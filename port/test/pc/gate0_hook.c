/*
 * Gate 0 PC trace hook (port/test/README.md, "Trace" and "PC trace").
 *
 * Linked only into the test binary build/pc-gate0/fe8pc (port/test/Makefile),
 * with -Wl,--wrap=VBlankIntrWait, -Wl,--wrap=UpdateKeyStatus,
 * -Wl,--wrap=OnVBlank, -Wl,--wrap=RenderBeginFrame, -Wl,--wrap=RenderEndFrame,
 * -Wl,--wrap=RenderLine, -Wl,--wrap=GetGameClock, -Wl,--wrap=SetGameTime and
 * a wrap for each BIOS copy and LZ77 call (CpuSet, CpuFastSet,
 * LZ77UnCompWram, LZ77UnCompVram) and for FlipBgVerticalScroll, and with
 * proc.o compiled with -finstrument-functions (port/test/pc/gate0.mk, this
 * build only). No file in src/ or include/ changes: the wraps catch the
 * game's own calls and references, which all come from other objects (bm.c,
 * main.c, bmio.c, gamecontrol.c, the battle animation loops, sioerror.c; every
 * GetGameClock and SetGameTime caller is outside time.o; the BIOS functions
 * are in port/bios/bios.c, and the game's calls, also through bmlib.c's
 * Decompress table, come from its own objects; FlipBgVerticalScroll is called
 * from savedraw.c only), and the frame step's calls into the renderer
 * (port/hw/frame.c into port/render/render.c). The lag replay runs a lag
 * frame's line interrupts with port/hw/frame.c's PortLagFrameLines, and the
 * raster-timing redraw a frame step's lines with PortRedrawFrameLines: both
 * are compiled into this binary only (FE8PC_GATE0, gate0.mk).
 *
 *   GATE0_OUT   trace path (JSON lines). Unset: the hook does nothing.
 *   GATE0_KEYS  key file (README.md, "Input file"). Unset: no keys.
 *   GATE0_TICKS ticks to record before exiting (default 300).
 *   GATE0_RAW   optional: raw bytes of the hashed buffers, as the GBA capture.
 *   GATE0_SHOT_TICKS optional: extra ticks with a frame15, as "a-b,c,...";
 *               every tick with tick % 60 == 0 always has one (as the GBA).
 *   GATE0_SHOTDIR optional: also write each frame15 frame as raw BGR555
 *               (240x160 little-endian u16, 76,800 bytes) to tickNNNNNN.555.
 *   GATE0_SRAM_IN optional: boot with this save in SRAM (LoadSramIn below).
 *   GATE0_SRAM  optional: at the end, write the 64 KiB at 0x0E000000 (SRAM)
 *               to this path, as the GBA capture does (README.md, "SRAM").
 *   GATE0_LAG_FROM: the lag replay (the lag check, below and in README.md).
 *   GATE0_CLOCK_FROM, GATE0_AFFINE_FROM: diagnostic replays (clock replay and
 *               affine replay, below and in README.md).
 *   GATE0_RASTER_FROM: the GBA capture's raster log, for the raster-timing
 *               redraw of frame15 frames (below and in README.md,
 *               "Raster-timing frames"); changes nothing in the run.
 *   GATE0_STATE_OUT/GATE0_STATE_AT, GATE0_STATE_IN: save states at the sample
 *               point (port/test/pc/savestate.c; README.md, "Save states").
 *   GATE0_SND_INDEX optional: one line "TICK V" per tick, V the index of the
 *               sound record (FE8PC_SND_RECORD) taken at the VBlank the tick
 *               waits for, after the tick's lag VBlanks (README.md, "Sound on
 *               any route"). Changes nothing in the run.
 *   GATE0_CHEAT optional, test coverage only: "durability", "funds",
 *               "convoy" or several joined with "+", the test cheats
 *               (README.md, "Test cheats"; CheatRefill below, the rule of
 *               port/test/oracle/test_cheat.lua): every unit's items and the
 *               convoy's back to their maximum uses, the party's gold at
 *               999,999, a rolling convoy, at each tick's sample, after the
 *               sample; refused (exit 4) in Eirika's Chapter 20 and final
 *               chapter. Unset or empty: nothing of it runs. This file is in
 *               the Gate 0 test binary only, so no played build has them.
 *
 * frame15 is the renderer's frame drawn while the tick waits for VBlank:
 * lines 0-159 of the frame step that follows the sample. The GBA screenshot
 * is of the same frame (it ends at line 160, before that tick's flush). The
 * value is RenderHashFrame555 of it, the hash the compare script takes of the
 * GBA PNG in 15-bit space.
 *
 * Fields are read through the game's own symbols, so a variable the host
 * placed away from its GBA address is still read where the PC game uses it.
 * Startup logs any such address. Three fields are file-static in the game
 * (gRNSeeds and gLCGRNValue in rng.c, sKeyStatusBuffer in hardware.c):
 * sKeyStatusBuffer is read through gKeyStatusPtr, the RNG state at its GBA
 * address, where gen_host_link.py places rng.o's .bss. The compare script
 * checks those addresses in the PC binary's symbol table.
 *
 * Each tick line also carries four counters, as the GBA capture's debugger
 * breakpoints do: onmain_calls (the port's --wrap=OnMain counter),
 * vblank_waits (calls to VBlankIntrWait), onvblank_calls (runs of OnVBlank)
 * and events (the tick's main-loop events, "Lag position" below).
 */
#define _FILE_OFFSET_BITS 64 /* the host build is 32-bit (-m32); raw dumps can pass 2 GiB */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <elf.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "port_hw.h"
#include "render.h"

/* Declared with neutral types: only the bytes are read. */
extern uint8_t gBmSt[], gPlaySt[], gLCDControlBuffer[], gPaletteBuffer[], gOam[];
extern uint8_t gBG0TilemapBuffer[], gBG1TilemapBuffer[], gBG2TilemapBuffer[], gBG3TilemapBuffer[];
extern uint8_t gGameClock[], gKeyStatusIgnoredSt[], gKeyComboResetEN[], gSoftwareResetFlag[];
extern uint8_t gMainCallback[];
extern uint8_t *gKeyStatusPtr; /* struct KeyStatusBuffer *, pointing at sKeyStatusBuffer */

void __real_VBlankIntrWait(void);
void __real_UpdateKeyStatus(void *keyStatus);
void __real_OnVBlank(void);
void __real_RenderBeginFrame(void);
void __real_RenderEndFrame(void);
void __real_RenderLine(int line);
uint32_t __real_GetGameClock(void);
void __real_SetGameTime(int newTime);
void __real_CpuSet(const void *src, void *dest, uint32_t control);
void __real_CpuFastSet(const void *src, void *dest, uint32_t control);
void __real_LZ77UnCompWram(const void *src, void *dest);
void __real_LZ77UnCompVram(const void *src, void *dest);
void __real_FlipBgVerticalScroll(void);
uint32_t RenderHashFrame555(const uint16_t *frame555); /* port/render/obj_line.h */
uint32_t PortAudio_NextRecord(void);                  /* port/audio/fifo_pc.c */

/* port/hw/frame.c, this binary only (FE8PC_GATE0, port/test/pc/gate0.mk); it
   calls PortGate0IrqDue (below) before every IRQ dispatch. */
void PortLagFrameLines(int afterVBlank);
void PortRedrawFrameLines(void);
void PortGate0IrqDue(int n);
extern void *gIRQHandlers[]; /* the game's IRQ table (irq.c), at its GBA address */

/* Save states (port/test/pc/savestate.c; README.md, "Save states"). */
int PortSnap_Setup(char *header, size_t cap, uint32_t ticks);
uint32_t PortSnap_AtSample(uint32_t tick, FILE *out, FILE *raw);
uint32_t PortSnap_TraceBase(void);
uint32_t PortSnap_LastSaveTick(void);
uint32_t PortSnap_GbaRawBase(FILE *f, uint32_t recordSize, uint32_t records);
char *PortSnap_Prologue(void);

#define REG_KEYINPUT_ADDR 0x04000130u

/* File-static in the game, so read at their GBA addresses (see above). */
#define GBA_gRNSeeds    ((uint8_t *)(uintptr_t)0x03000000u)
#define GBA_gLCGRNValue ((uint8_t *)(uintptr_t)0x03000008u)
#define GBA_sTrapPool   ((uint8_t *)(uintptr_t)0x0203A614u) /* bmtrap.c, static struct Trap[64] */

struct Field
{
    const char *name;
    const uint8_t *host;
    uint32_t gbaAddr;
    uint32_t size;
};

/* Same order, addresses and sizes as port/test/oracle/gate0_capture.lua. */
static const struct Field sScalars[] = {
    { "gGameClock",          gGameClock,          0x03000014, 4 },
    { "gKeyStatusIgnoredSt", gKeyStatusIgnoredSt, 0x03000010, 2 },
    { "gKeyComboResetEN",    gKeyComboResetEN,    0x0300000C, 1 },
    { "gSoftwareResetFlag",  gSoftwareResetFlag,  0x0300001A, 1 },
    { "gMainCallback",       gMainCallback,       0x02024CB8, 4 },
    { "gLCGRNValue",         GBA_gLCGRNValue,     0x03000008, 4 },
};
static struct Field sRaws[] = {
    { "gBmSt",            gBmSt,        0x0202BCB0, 0x40 },
    { "gPlaySt",          gPlaySt,      0x0202BCF0, 0x4C },
    { "gRNSeeds",         GBA_gRNSeeds, 0x03000000, 0x06 },
    { "sKeyStatusBuffer", NULL,         0x02024CC0, 0x14 }, /* host set in Setup */
};
static const struct Field sHashed[] = {
    { "gLCDControlBuffer", gLCDControlBuffer, 0x03003080, 0x69 },
    { "gPaletteBuffer",    gPaletteBuffer,    0x020228A8, 0x400 },
    { "gBG0TilemapBuffer", gBG0TilemapBuffer, 0x02022CA8, 0x800 },
    { "gBG1TilemapBuffer", gBG1TilemapBuffer, 0x020234A8, 0x800 },
    { "gBG2TilemapBuffer", gBG2TilemapBuffer, 0x02023CA8, 0x800 },
    { "gBG3TilemapBuffer", gBG3TilemapBuffer, 0x020244A8, 0x800 },
    { "gOam",              gOam,              0x03003140, 0x400 },
};
/* FNV-1a 32 only, no raw dump (integration 11; README.md, "Trace"): same table
   as gate0_capture.lua's hashedOnly. The raw record (sHashed, savestate.c's
   RAW_RECORD) is unchanged. */
static const struct Field sHashedOnly[] = {
    { "sTrapPool",         GBA_sTrapPool,     0x0203A614, 0x200 },
};

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

/* The test cheats (GATE0_CHEAT; README.md, "Test cheats"). The game's unit
   arrays and convoy, at their GBA addresses (src/bmunit.c, include/variables.h;
   struct Unit is 0x48 bytes on both sides, layout-check), the item table
   (src/data_items.c; struct ItemData is 0x24 bytes on both sides, checked by
   CheatSetup through each entry's number) and the game's own item functions
   (src/bmitem.c), which read gItemData. */
extern uint8_t gUnitArrayBlue[], gUnitArrayRed[], gUnitArrayGreen[], gUnitArrayPurple[];
extern uint8_t gConvoyItemArray[];
extern const uint8_t gItemData[];
int GetItemAttributes(int item);
int GetItemMaxUses(int item);
int MakeNewItem(int item);

static const struct
{
    uint8_t *units;
    unsigned count;
} sCheatUnits[] = {
    { gUnitArrayBlue, 62 },
    { gUnitArrayRed, 50 },
    { gUnitArrayGreen, 20 },
    { gUnitArrayPurple, 5 },
};
#define CHEAT_UNIT_SIZE      0x48u
#define CHEAT_UNIT_ITEMS     0x1Eu /* struct Unit items[5] */
#define CHEAT_IA_UNBREAKABLE 0x8   /* include/bmitem.h IA_UNBREAKABLE */
#define CHEAT_CONVOY_COUNT   100u  /* include/bmcontainer.h CONVOY_ITEM_COUNT */
#define CHEAT_ITEM_SIZE      0x24u /* struct ItemData (include/bmitem.h) */
#define CHEAT_ITEM_COUNT     0xCEu /* gItemData's entries, ITEM_NONE to ITEM_UNK_CD (src/data_items.c) */
#define CHEAT_MSG_DUMMY      0x403u /* MSG_403, "Dummy": a placeholder item's name (texts/texts.txt) */
#define CHEAT_GOLD_MAX       999999u /* SetPartyGoldAmount's cap (src/bmgold.c) */
#define CHEAT_DURABILITY     1u
#define CHEAT_FUNDS          2u
#define CHEAT_CONVOY         4u
static const char *const sCheatNames[] = { "durability", "funds", "convoy" }; /* CHEAT_* bit order */
static const char *sCheat;         /* GATE0_CHEAT when set: its name in that order (sCheatName) */
static char sCheatName[32];
static unsigned sCheatOn;          /* its CHEAT_* bits */
static uint32_t sCheatWords, sCheatTicks, sCheatFundsTicks, sCheatPlaced; /* the run's totals */
/* The convoy cheat: the items it places (ids whose name is not MSG_403) and
   its cursor, the items placed so far (session state in tools/savestate.py: a
   resumed run takes it from its photo's prologue line, CheatResume). */
static uint8_t sCheatItems[CHEAT_ITEM_COUNT];
static unsigned sCheatItemCount;
static uint32_t sCheatConvoy;

static int sState; /* 0 not set up, 1 active, -1 inactive */
static FILE *sOut, *sRaw;
static FILE *sSndIndex; /* GATE0_SND_INDEX */
static uint16_t *sMasks;
static uint32_t sMaskCount, sTicks = 300, sTick, sLastTickFrame;
static uint32_t sVBlankWaits, sOnVBlankCalls;
static const char *sKeysPath = "";
static const char *sShotDir;
/* Every range of GATE0_SHOT_TICKS (heap, one per comma-separated item; H-21:
   a fixed [64] kept the first 64 and dropped the rest without a word, so the
   compare had fewer PC frames than GBA frames and said MATCH). */
static uint32_t (*sShotRanges)[2];
static unsigned sShotRangeCount;

static void Fail(const char *msg, const char *arg);
static void AffineFinishSample(void);
static void RasterEndFrame(void);
static void RasterFinishSample(uint32_t tick);
static int RasterRedrawing(void);
static int sInVBlank; /* see "Lag position" below */

/* GATE0_SHOT_TICKS: "a-b,c,..." */
static void ReadShotTicks(const char *s)
{
    size_t n = 1;
    const char *c;

    for (c = s; *c != '\0'; c++)
        if (*c == ',')
            n++;
    sShotRanges = calloc(n, sizeof(*sShotRanges));
    if (sShotRanges == NULL)
        Fail("no memory for GATE0_SHOT_TICKS: ", s);
    while (*s != '\0')
    {
        if (sShotRangeCount >= n)
            Fail("bad GATE0_SHOT_TICKS: ", s);
        char *end;
        unsigned long a = strtoul(s, &end, 0), b = a;

        if (end == s)
            Fail("bad GATE0_SHOT_TICKS: ", s);
        if (*end == '-')
            b = strtoul(end + 1, &end, 0);
        sShotRanges[sShotRangeCount][0] = (uint32_t)a;
        sShotRanges[sShotRangeCount][1] = (uint32_t)b;
        sShotRangeCount++;
        s = end;
        if (*s == ',')
            s++;
        else if (*s != '\0')
            Fail("bad GATE0_SHOT_TICKS: ", s);
    }
}

static int IsShotTick(uint32_t tick)
{
    unsigned i;

    if (tick % 60 == 0)
        return 1;
    for (i = 0; i < sShotRangeCount; i++)
        if (tick >= sShotRanges[i][0] && tick <= sShotRanges[i][1])
            return 1;
    return 0;
}

/* Close the tick's line with the frame drawn while it waited. */
static void FinishSample(uint32_t tick)
{
    if (IsShotTick(tick) && gRenderEnabled)
    {
        fprintf(sOut, ",\"frame15\":\"%08x\"", gRenderFrameHash);
        AffineFinishSample();
        RasterFinishSample(tick);
        if (sShotDir != NULL)
        {
            char path[512];
            FILE *f;

            snprintf(path, sizeof(path), "%s/tick%06u.555", sShotDir, tick);
            f = fopen(path, "wb");
            if (f == NULL)
                Fail("cannot open ", path);
            fwrite(gRenderFrame555, 1, sizeof(gRenderFrame555), f); /* x86: little-endian */
            fclose(f);
        }
    }
    else
        fprintf(sOut, ",\"frame15\":null");
    fprintf(sOut, "}\n");
    /* Keep every complete tick if the run faults later (P-11). A fault exits
       with _exit, so a line still open in the buffer is never written. */
    fflush(sOut);
    if (sRaw != NULL)
        fflush(sRaw);
    if (sSndIndex != NULL)
        fflush(sSndIndex);
}

static uint32_t Fnv1a(const uint8_t *p, uint32_t n)
{
    uint32_t h = 0x811C9DC5u;
    while (n--)
        h = (h ^ *p++) * 0x01000193u;
    return h;
}

static void Fail(const char *msg, const char *arg)
{
    fprintf(stderr, "gate0_hook: %s%s\n", msg, arg ? arg : "");
    exit(4);
}

/* The test cheat's refill of n item words at p, then their FNV-1a 32 on from h
   (memory order). The rule of port/test/oracle/test_cheat.lua: an empty slot,
   an item with IA_UNBREAKABLE and one whose maximum is 0 stay as they are;
   any other gets its uses set to GetItemMaxUses (also from above it). */
static uint32_t CheatSlots(uint8_t *p, unsigned n, int live, uint32_t h, uint32_t *changed)
{
    unsigned i;

    for (i = 0; i < n; i++)
    {
        uint16_t w;

        memcpy(&w, p + 2 * i, 2);
        if (live && w != 0 && (GetItemAttributes(w) & CHEAT_IA_UNBREAKABLE) == 0)
        {
            int max = GetItemMaxUses(w);

            if (max != 0 && (w >> 8) != max)
            {
                w = (uint16_t)((w & 0xFFu) | ((unsigned)max << 8));
                memcpy(p + 2 * i, &w, 2);
                (*changed)++;
            }
        }
        h = (h ^ (w & 0xFFu)) * 0x01000193u;
        h = (h ^ (w >> 8)) * 0x01000193u;
    }
    return h;
}

/* GATE0_CHEAT (README.md, "Test cheats"): the cheats joined with "+", in any
   order, each once; sCheatName gets them in sCheatNames' order, as
   test_cheat.lua's C.check names them. Then the convoy cheat's item list: the
   ids 0x01 to 0xCD whose name is not MSG_403 (177 of them). */
static void CheatSetup(const char *value)
{
    const char *p = value;
    unsigned i;

    while (1)
    {
        size_t n = strcspn(p, "+");
        unsigned bit = 0;

        for (i = 0; i < COUNT(sCheatNames); i++)
            if (strlen(sCheatNames[i]) == n && strncmp(p, sCheatNames[i], n) == 0)
                bit = 1u << i;
        if (bit == 0)
            Fail("GATE0_CHEAT: the test cheats are durability, funds and convoy, joined with + (README.md, \"Test "
                 "cheats\"), not ", value);
        if (sCheatOn & bit)
            Fail("GATE0_CHEAT names a cheat twice: ", value);
        sCheatOn |= bit;
        if (p[n] == '\0')
            break;
        p += n + 1;
    }
    for (i = 0; i < COUNT(sCheatNames); i++)
        if (sCheatOn & (1u << i))
        {
            if (sCheatName[0] != '\0')
                strcat(sCheatName, "+");
            strcat(sCheatName, sCheatNames[i]);
        }
    sCheat = sCheatName;
    for (i = 1; i < CHEAT_ITEM_COUNT; i++)
    {
        const uint8_t *d = gItemData + i * CHEAT_ITEM_SIZE;
        uint16_t name;

        if (d[0x06] != i) /* struct ItemData number */
            Fail("GATE0_CHEAT: gItemData's entries are not 0x24 bytes apart (an entry's number is not its index)", NULL);
        memcpy(&name, d, 2); /* nameTextId */
        if (name != CHEAT_MSG_DUMMY)
            sCheatItems[sCheatItemCount++] = (uint8_t)i;
    }
}

/* The convoy cheat (test_cheat.lua's convoy()): while an army is loaded (some
   blue unit's pCharacterData is set), every empty convoy slot (0) gets the
   next item of the list as the game makes a new one (MakeNewItem), the cursor
   counting them and going round the list. Returns the items placed. */
static uint32_t CheatConvoy(void)
{
    unsigned i;
    uint32_t placed = 0;

    for (i = 0; i < 62; i++)
    {
        uint32_t character;

        memcpy(&character, gUnitArrayBlue + i * CHEAT_UNIT_SIZE, 4);
        if (character != 0)
            break;
    }
    if (i == 62)
        return 0;
    for (i = 0; i < CHEAT_CONVOY_COUNT; i++)
    {
        uint16_t w;

        memcpy(&w, gConvoyItemArray + 2 * i, 2);
        if (w != 0)
            continue;
        w = (uint16_t)MakeNewItem(sCheatItems[sCheatConvoy % sCheatItemCount]);
        memcpy(gConvoyItemArray + 2 * i, &w, 2);
        sCheatConvoy++;
        placed++;
    }
    return placed;
}

/* The test cheats at the tick boundary (README.md, "Test cheats"; the rule and
   the order of test_cheat.lua's refill()), and their fields on the tick line:
   convoy (above); durability, every item of every unit (the five slots of each
   struct Unit whose pCharacterData is set, in the blue, red, green and purple
   arrays) and of the convoy back to its maximum uses; funds, the party's gold
   (gPlaySt.partyGoldAmount, +0x08) set to 999,999. "cheat_items" is the FNV-1a
   32 of all 137 units' item bytes (whatever pCharacterData is) and then the
   convoy's, after them; "cheat_refills" the words durability changed,
   "cheat_funds" 1 when funds changed the gold, "cheat_convoy" the cursor. Not
   in Eirika's Chapter 20 and final chapter (the user's decision of 2026-10-22:
   they are played fair): with gPlaySt.chapterIndex 0x14 to 0x16, off Ephraim's
   route (chapterModeIndex 3, where such an index is a scene's map) and before
   the game is complete (chapterStateBits' PLAY_FLAG_COMPLETE, 0x20: the
   post-game of a clear save), the run stops here, before any change, as
   test_cheat.lua stops the GBA tools at the same sample. */
static void CheatRefill(void)
{
    uint32_t h = 0x811C9DC5u, changed = 0, placed = 0, funds = 0;
    unsigned a, i, chapter = gPlaySt[0x0E]; /* gPlaySt.chapterIndex (include/types.h) */

    if (chapter >= 0x14 && chapter <= 0x16 && gPlaySt[0x1B] != 3 /* chapterModeIndex, CHAPTER_MODE_EPHRAIM */
        && (gPlaySt[0x14] & 0x20) == 0) /* chapterStateBits, PLAY_FLAG_COMPLETE */
    {
        fprintf(stderr, "gate0_hook: GATE0_CHEAT=%s refused at tick %u: chapterIndex 0x%02x is Eirika's Chapter 20 "
            "or final chapter (0x14 to 0x16, the game not complete), which are played fair (README.md, \"Test "
            "cheats\")\n", sCheat, sTick, chapter);
        exit(4);
    }
    if (sCheatOn & CHEAT_CONVOY)
        placed = CheatConvoy();
    for (a = 0; a < COUNT(sCheatUnits); a++)
        for (i = 0; i < sCheatUnits[a].count; i++)
        {
            uint8_t *u = sCheatUnits[a].units + i * CHEAT_UNIT_SIZE;
            uint32_t character;

            memcpy(&character, u, 4); /* pCharacterData: a 32-bit pointer, as on the GBA */
            h = CheatSlots(u + CHEAT_UNIT_ITEMS, 5, (sCheatOn & CHEAT_DURABILITY) && character != 0, h, &changed);
        }
    h = CheatSlots(gConvoyItemArray, CHEAT_CONVOY_COUNT, (sCheatOn & CHEAT_DURABILITY) != 0, h, &changed);
    if (sCheatOn & CHEAT_FUNDS)
    {
        uint32_t gold;

        memcpy(&gold, gPlaySt + 0x08, 4);
        if (gold != CHEAT_GOLD_MAX)
        {
            gold = CHEAT_GOLD_MAX;
            memcpy(gPlaySt + 0x08, &gold, 4);
            funds = 1;
        }
    }
    fprintf(sOut, ",\"cheat_items\":\"%08x\"", h);
    if (sCheatOn & CHEAT_DURABILITY)
        fprintf(sOut, ",\"cheat_refills\":%u", changed);
    if (sCheatOn & CHEAT_FUNDS)
        fprintf(sOut, ",\"cheat_funds\":%u", funds);
    if (sCheatOn & CHEAT_CONVOY)
        fprintf(sOut, ",\"cheat_convoy\":%u", sCheatConvoy);
    sCheatWords += changed;
    sCheatTicks += changed != 0;
    sCheatFundsTicks += funds;
    sCheatPlaced += placed;
}

/* A resumed run (GATE0_STATE_IN; README.md, "Save states"): the photo's
   prologue line (its tick T-1, written by the run it was taken in) shows which
   cheats that run had, by their fields, and the convoy cursor after T-1. The
   run stops (exit 4) when they are not this one's, as the GBA capture does
   with its photo's meta.json "cheat", and takes the cursor. */
static void CheatResume(void)
{
    char *line = PortSnap_Prologue();
    unsigned i, had = 0;
    const char *const fields[] = { ",\"cheat_refills\":", ",\"cheat_funds\":", ",\"cheat_convoy\":" };

    if (line == NULL)
        return;
    for (i = 0; i < COUNT(fields); i++)
        if (strstr(line, fields[i]) != NULL)
            had |= 1u << i;
    if ((strstr(line, ",\"cheat_items\":") != NULL) != (sCheatOn != 0) || had != sCheatOn)
    {
        fprintf(stderr, "gate0_hook: the save state's run had other test cheats (its prologue line's fields: %s%s%s%s) "
            "than this one, GATE0_CHEAT=%s (README.md, \"Test cheats\")\n",
            strstr(line, ",\"cheat_items\":") ? "cheat_items" : "none", had & CHEAT_DURABILITY ? " cheat_refills" : "",
            had & CHEAT_FUNDS ? " cheat_funds" : "", had & CHEAT_CONVOY ? " cheat_convoy" : "", sCheat ? sCheat : "");
        exit(4);
    }
    if (sCheatOn & CHEAT_CONVOY)
        sCheatConvoy = (uint32_t)strtoul(strstr(line, fields[2]) + strlen(fields[2]), NULL, 10);
    free(line);
}

/* README.md, "Input file". Lines of any length (getline): a header comment
   longer than the old 256-byte buffer was split, and its rest stopped the run
   as a bad entry (cov9 H-A). */
static void ReadKeys(const char *path)
{
    FILE *f = fopen(path, "r");
    char *line = NULL;
    size_t lineCap = 0;
    uint32_t cap = 0, lineNo = 0;

    if (f == NULL)
        Fail("cannot open key file ", path);
    while (getline(&line, &lineCap, f) > 0)
    {
        char *s = line, *end, *hash = strchr(line, '#');
        unsigned long mask, n = 1;

        lineNo++;
        if (hash != NULL)
            *hash = '\0';
        while (isspace((unsigned char)*s))
            s++;
        end = s + strlen(s);
        while (end > s && isspace((unsigned char)end[-1]))
            *--end = '\0';
        if (*s == '\0')
            continue;
        if (!isxdigit((unsigned char)*s))
            Fail("bad key file entry: ", line);
        mask = strtoul(s, &end, 16);
        if (*end == '*')
        {
            char *numEnd;
            n = strtoul(end + 1, &numEnd, 10);
            end = numEnd;
        }
        if (*end != '\0' || mask > 0x3FF)
        {
            fprintf(stderr, "gate0_hook: %s:%u: bad entry\n", path, lineNo);
            exit(4);
        }
        while (n--)
        {
            if (sMaskCount == cap)
            {
                cap = cap ? cap * 2 : 1024;
                sMasks = realloc(sMasks, cap * sizeof(*sMasks));
            }
            sMasks[sMaskCount++] = (uint16_t)mask;
        }
    }
    free(line);
    fclose(f);
}

static uint16_t MaskFor(uint32_t tick)
{
    return tick < sMaskCount ? sMasks[tick] : 0;
}

/* The tick in progress (the next one to be sampled), for the BIOS-area probe
   (port/hw/nullprobe.c, FE8PC_NULLPROBE=1), which declares it weak. */
uint32_t PortGate0Tick(void)
{
    return sTick;
}

/*
 * Start from a save (README.md, "Chapter saves"): GATE0_SRAM_IN=PATH puts the
 * first 32 KiB of PATH into SRAM (0x0E000000 under D1-a) before the game first
 * touches it, the same bytes the GBA runner gives mGBA as its save
 * (oracle/mgba_lua_run.c). Setup runs at the boot-time UpdateKeyStatus
 * (main.c:53), before SramInit (main.c:55); a load after SramInit stops the
 * run. The upper 32 KiB stays 0xFF, as before (D-18). PATH is only read;
 * GATE0_SRAM still writes the SRAM at the end. The trace header names the file
 * and the FNV-1a 32 of the 32 KiB ("sram_in", "sram_fnv"), as the GBA capture
 * does, and the compare checks that both sides started from the same bytes.
 */
#define SRAM_IN_SIZE 0x8000u
extern uint8_t gBoolSramWorking; /* bool, set by SramInit (bmsave-lib.c) */
static char sSramHeader[1600]; /* also the "resume" block of a save state run */

static void LoadSramIn(void)
{
    const char *path = getenv("GATE0_SRAM_IN");
    uint8_t *sram = (uint8_t *)(uintptr_t)0x0E000000u;
    FILE *f;
    long size = -1;
    uint32_t hash;

    if (path == NULL || *path == '\0')
        return;
    if (gBoolSramWorking)
        Fail("GATE0_SRAM_IN comes too late: SramInit has already run", NULL);
    f = fopen(path, "rb");
    if (f == NULL)
        Fail("cannot open GATE0_SRAM_IN ", path);
    if (fseek(f, 0, SEEK_END) == 0)
        size = ftell(f);
    if ((size != SRAM_IN_SIZE && size != 2 * SRAM_IN_SIZE) || fseek(f, 0, SEEK_SET) != 0
        || fread(sram, 1, SRAM_IN_SIZE, f) != SRAM_IN_SIZE)
        Fail("GATE0_SRAM_IN must be a 32 KiB or 64 KiB save: ", path);
    fclose(f);
    hash = Fnv1a(sram, SRAM_IN_SIZE);
    snprintf(sSramHeader, sizeof(sSramHeader), ",\"sram_in\":\"%s\",\"sram_fnv\":\"%08x\"", path, hash);
    PortLog("[gate0] SRAM from %s (32768 bytes, fnv1a %08x)\n", path, hash);
}

/*
 * Diagnostic only (README.md, "Clock replay"): GATE0_CLOCK_FROM=GBA trace.
 * Before each OnVBlank that follows tick t's sample, gGameClock is set to the
 * GBA trace's value at tick t, so OnVBlank's increment and the next OnMain
 * read the GBA's clock, lag VBlanks included. This changes the PC run: use it
 * only to show which differences come from the clock. The trace header then
 * names the file ("clock_from"), and the compare leaves the clock out.
 */
static uint32_t *sGbaClock, sGbaClockCount;

static void ReadGbaClock(const char *path)
{
    FILE *f = fopen(path, "r");
    char *line = NULL;
    size_t cap = 0, have = 0;

    if (f == NULL)
        Fail("cannot open ", path);
    while (getline(&line, &cap, f) > 0)
    {
        const char *s = strstr(line, "\"gGameClock\":\"");
        if (s == NULL)
            continue;
        if (sGbaClockCount == have)
        {
            have = have ? have * 2 : 4096;
            sGbaClock = realloc(sGbaClock, have * sizeof(*sGbaClock));
        }
        sGbaClock[sGbaClockCount++] = (uint32_t)strtoul(s + 14, NULL, 16);
    }
    free(line);
    fclose(f);
    if (sGbaClockCount == 0)
        Fail("no gGameClock values in ", path);
    PortLog("[gate0] clock replay: %u GBA clock values from %s\n", sGbaClockCount, path);
}

/*
 * Diagnostic only (README.md, "Affine replay", decision P2.2b):
 * GATE0_AFFINE_FROM=GBA raw dump (gba.raw, the GATE0_RAW of the GBA capture).
 *
 * The frame of tick t is drawn with the registers OnVBlank flushed after tick
 * t-1's sample. For each frame15 frame of a tick t >= 1 whose GBA bg2affin at
 * tick t-1 (gLCDControlBuffer +0x48, read from the dump) differs from the
 * PC's, the hook sets the BG2 affine registers (BG2PA-BG2Y, 0x04000020-
 * 0x0400002F, the bytes FlushLCDControl copies bg2affin to, hardware.c) to the
 * GBA's values at the start of line 0, so lines 0-159 of that frame are drawn
 * with them. That frame is the trace's frame15. After line 159 the hook draws
 * the same frame again, at once, with the PC's own register values
 * ("frame15_pc"; the compare checks it equals the PC trace's frame15, which
 * ties the two runs to the same frame), puts the replayed frame back as the
 * frame of record and leaves the PC's values in the registers. Nothing the game
 * reads changes, so every other field must equal the PC trace's; the compare
 * checks that too.
 *
 * Each frame15 tick of such a run carries "affine":
 *   "gba"      drawn with the GBA's bg2affin of tick t-1; "frame15_pc" follows.
 *   "equal"    the GBA's bg2affin of tick t-1 equals the PC's: nothing replaced.
 *   "regs"     not replaced: at line 0 the BG2 affine registers did not hold
 *              the PC's own bg2affin of tick t-1 (something other than the
 *              flush wrote them), so the frame is not the case P2.2b covers.
 *   "midframe" replaced, but the game wrote the registers during the frame:
 *              its value is kept, the frame is not usable and has no frame15_pc.
 *   "none"     tick 0, or no GBA record for tick t-1: nothing replaced.
 */
#define BG2_AFFINE_REGS ((uint8_t *)(uintptr_t)0x04000020u)
#define LCD_BG2AFFIN    0x48 /* gLCDControlBuffer.bg2affin, struct BgAffineDstData (16 bytes) */

enum { AFFINE_IDLE, AFFINE_PENDING, AFFINE_REPLACED };

static FILE *sAffineFrom;
static const char *sAffinePath;
static uint32_t sAffineRecord, sAffineRecords; /* bytes per tick in the dump, ticks in it */
static uint32_t sAffineBase;                   /* tick of its first record (a resumed capture's: T-1) */
static uint8_t sAffinePcPrev[16], sAffinePcCur[16]; /* the PC's bg2affin at ticks t-1 and t */
static uint8_t sAffineGba[16], sAffineRegs[16];
static int sAffineStep;
static const char *sAffineMark;
static uint32_t sAffinePcHash;
static uint32_t sAffineCounts[5]; /* gba, equal, regs, midframe, none */
static uint16_t sAffineFrame[RENDER_FRAME_HEIGHT][RENDER_FRAME_WIDTH];

static void OpenGbaAffine(const char *path)
{
    off_t size;
    unsigned i;

    if (sHashed[0].host != gLCDControlBuffer)
        Fail("field table out of order", NULL);
    for (i = 0, sAffineRecord = 0; i < COUNT(sHashed); i++)
        sAffineRecord += sHashed[i].size;
    sAffinePath = path;
    sAffineFrom = fopen(path, "rb");
    if (sAffineFrom == NULL || fseeko(sAffineFrom, 0, SEEK_END) != 0 || (size = ftello(sAffineFrom)) < 0)
        Fail("cannot read ", path);
    if (size % sAffineRecord != 0)
        Fail("not a raw dump (size is not a whole number of ticks): ", path);
    sAffineRecords = (uint32_t)(size / sAffineRecord);
    sAffineBase = PortSnap_GbaRawBase(sAffineFrom, sAffineRecord, sAffineRecords); /* 0 unless resumed */
    sAffineRecords += sAffineBase;
    PortLog("[gate0] affine replay: GBA bg2affin of %u ticks from %s\n", sAffineRecords, path);
}

static int ReadGbaAffine(uint32_t tick)
{
    if (tick >= sAffineRecords || tick < sAffineBase)
        return 0;
    if (fseeko(sAffineFrom, (off_t)(tick - sAffineBase) * sAffineRecord + LCD_BG2AFFIN, SEEK_SET) != 0
        || fread(sAffineGba, 1, sizeof(sAffineGba), sAffineFrom) != sizeof(sAffineGba))
        Fail("cannot read ", sAffinePath);
    return 1;
}

/* After tick t's sample, before its frame step. */
static void AffinePrepare(uint32_t tick)
{
    memcpy(sAffinePcPrev, sAffinePcCur, sizeof(sAffinePcPrev));
    memcpy(sAffinePcCur, gLCDControlBuffer + LCD_BG2AFFIN, sizeof(sAffinePcCur));
    sAffineStep = AFFINE_IDLE;
    sAffineMark = NULL;
    if (!IsShotTick(tick) || !gRenderEnabled)
        return;
    if (tick == 0 || !ReadGbaAffine(tick - 1))
        sAffineMark = "none";
    else if (memcmp(sAffineGba, sAffinePcPrev, sizeof(sAffineGba)) == 0)
        sAffineMark = "equal";
    else
        sAffineStep = AFFINE_PENDING; /* decided at line 0 */
}

/* Line 0 of the frame step after the sample: the frame of that tick. */
void __wrap_RenderBeginFrame(void)
{
    if (sAffineStep == AFFINE_PENDING)
    {
        if (memcmp(BG2_AFFINE_REGS, sAffinePcPrev, sizeof(sAffinePcPrev)) != 0)
        {
            sAffineMark = "regs";
            sAffineStep = AFFINE_IDLE;
        }
        else
        {
            memcpy(sAffineRegs, BG2_AFFINE_REGS, sizeof(sAffineRegs));
            memcpy(BG2_AFFINE_REGS, sAffineGba, sizeof(sAffineGba));
            sAffineMark = "gba";
            sAffineStep = AFFINE_REPLACED;
        }
    }
    __real_RenderBeginFrame();
}

/* After line 159 (hash and RGBA done), before line 159's HBlank IRQ. */
void __wrap_RenderEndFrame(void)
{
    int line;

    if (RasterRedrawing())
        return; /* a raster-timing redraw's line 159: nothing to output (RasterRedraw) */
    __real_RenderEndFrame();
    RasterEndFrame(); /* raster-timing frames, see OpenGbaRaster (never with the affine replay) */
    if (sAffineStep != AFFINE_REPLACED)
        return;
    sAffineStep = AFFINE_IDLE;
    if (memcmp(BG2_AFFINE_REGS, sAffineGba, sizeof(sAffineGba)) != 0)
    {
        sAffineMark = "midframe"; /* the game's write stands */
        return;
    }
    /* The same frame with the PC's own values: lines 0-159 again, with the
       renderer's line-0 latch, as the frame step draws them. */
    memcpy(sAffineFrame, gRenderFrame555, sizeof(sAffineFrame));
    memcpy(BG2_AFFINE_REGS, sAffineRegs, sizeof(sAffineRegs));
    __real_RenderBeginFrame();
    for (line = 0; line < RENDER_FRAME_HEIGHT; line++)
        __real_RenderLine(line);
    sAffinePcHash = RenderHashFrame555(&gRenderFrame555[0][0]);
    memcpy(gRenderFrame555, sAffineFrame, sizeof(sAffineFrame));
}

/* Called from FinishSample on a frame15 tick. */
static void AffineFinishSample(void)
{
    static const char *const names[] = { "gba", "equal", "regs", "midframe", "none" };
    unsigned i;

    if (sAffineMark == NULL)
        return;
    fprintf(sOut, ",\"affine\":\"%s\"", sAffineMark);
    if (strcmp(sAffineMark, "gba") == 0)
        fprintf(sOut, ",\"frame15_pc\":\"%08x\"", sAffinePcHash);
    for (i = 0; i < COUNT(names); i++)
        if (strcmp(sAffineMark, names[i]) == 0)
            sAffineCounts[i]++;
    sAffineMark = NULL;
}

/*
 * Raster-timing frames (README.md, "Raster-timing frames"; port-plan
 * DECISIONS.md 2026-10-20 afternoon, item 9; DIVERGENCES P-17):
 * GATE0_RASTER_FROM=the GBA capture's raster log (gba_raster.bin, written by
 * oracle/mgba_lua_run.c with MGBA_RUN_RASTERLOG). Nothing in the run changes.
 *
 * The GBA draws a frame while its CPU runs on: OnVBlank and OnMain run into the
 * frame's lines, so a line can show the main loop's memory before or after a
 * write, as CPU time decides. The PC runs OnMain in zero time, then steps the
 * frame (D-15), so every line shows the tick's last state. For each frame15
 * frame the GBA's log has the frame's line events (an HBlank or VCount handler
 * entered, a line drawn), every store that changed a watched halfword
 * (palette RAM, VRAM and OAM, what the renderer reads; the RAM ranges the line
 * handlers read; REG_IE and REG_DISPSTAT's IRQ enables, which decide whether
 * they run at all, and the BG scroll registers, which OnMain also writes
 * directly: port/test/Makefile GBA_RASTER_RAM), each with the number of line
 * events before it, and each watched region's FNV-1a at the frame's end.
 *
 * On a frame15 tick whose GBA frame has such stores the hook draws the frame
 * again twice, after line 159 (RasterEndFrame), from the machine as it was at
 * the start of the frame step (all of EWRAM, IWRAM, I/O, palette RAM, VRAM
 * and OAM kept then), with the frame step's own line code
 * (PortRedrawFrameLines: the game's HBlank and VCount handlers run live):
 *   1. the control redraw, as is: it must be the PC's own frame, so the redraw
 *      reproduces the frame step;
 *   2. the GBA-timing redraw: each watched halfword the GBA stored in the
 *      frame put back to the GBA's value from before the frame (newest store
 *      first, each one's new value checked against the PC's memory), then each
 *      store made again just before the line event it came before on the GBA
 *      (its old value checked too). Its hash is "frame15_raster". The stores
 *      due are also made before every IRQ check (PortGate0IrqDue, from
 *      port/hw/frame.c's Irq before its IME and IE test, and from LineStart and
 *      LineHBlank before their REG_DISPSTAT tests), so a REG_IE store turns a
 *      handler off (or on) for the rest of the redraw from where the GBA's
 *      main loop made it: the subtitle-help darkener that OnMain removes while
 *      the GBA has already run it on the first VBlank lines (G7); and the
 *      REG_DISPSTAT store of OnVBlank's flush turns the HBlank IRQ on where the
 *      GBA made it (C10-10 (b)). A store that turns an IRQ source on waits for
 *      the check of the GBA's next line event (PortGate0IrqDue). An IRQ that
 *      a test refuses is no line event, as on the GBA.
 * Before that, every watched region with GBA stores must equal the GBA's at
 * the frame's end (so the GBA's values come from a machine in the PC's own
 * state: a PC that drew a wrong window shape or wrong text is not the GBA's
 * there, and fails), and must be unchanged by the PC's frame step (only the
 * main loop writes it; a line handler that wrote it would be undone by the
 * replay). After both, the frame step's memory, the frame of record, its RGBA
 * and hash are put back, so nothing the game reads changes.
 *
 * Each frame15 tick with a GBA block gets "raster" (and "raster_rec", the
 * block's CRC-32, which the compare matches with the GBA trace's "raster"):
 *   "gba"        redrawn; "frame15_raster" follows. The line events of the
 *                redraw were the GBA's, event for event.
 *   "nostores"   the GBA stored nothing watched during the frame: nothing to replay.
 *   "differs"    a watched region with GBA stores is not the GBA's at the end
 *                ("raster_note" names it).
 *   "handler"    the PC's frame step changed a watched region with GBA stores.
 *   "incomplete" the GBA log could not account for a region's change, or ran
 *                out of room for line events: nothing is replayed from it.
 *   "control"    the control redraw is not the PC's frame.
 *   "rollback"   a GBA store's value is not what the PC's memory holds there.
 *   "slots"      the redraw's line events are not the GBA's ("raster_note":
 *                the first that differs), for example a handler the GBA took
 *                from an IF bit latched while IE was off (P-25).
 * A tick without a block gets "raster":"none". Only "gba" lets the compare
 * label a frame (a raster-timing frame).
 */
/* Full since fix round 13 (6 RAM and I/O ranges, the sixth the map
   animations' scanline buffer pointers for P-42, plus the 3 video regions):
   the next watched region needs this and mgba_lua_run.c's RASTER_MAX_REGIONS
   raised together. */
#define RR_MAX_REGIONS 9
#define RR_MAX_EVENTS 2048
#define RR_MEM_PARTS 6

struct RrRegion
{
    char name[12];
    uint32_t addr, size, fnv;
};

struct RrStore /* one store of the GBA's log, 16 bytes */
{
    uint32_t addr;
    uint16_t oldValue, newValue, pos, flags;
    uint32_t pc;
};

/* The GBA memory the frame step and its handlers can change (D1-a: at the GBA addresses). */
static const uint32_t kRrMem[RR_MEM_PARTS][2] = {
    { 0x02000000u, 0x40000u }, { 0x03000000u, 0x8000u }, { 0x04000000u, 0x400u },
    { 0x05000000u, 0x400u },   { 0x06000000u, 0x18000u }, { 0x07000000u, 0x400u },
};
#define RR_MEM_BYTES (0x40000u + 0x8000u + 0x400u + 0x400u + 0x18000u + 0x400u)

enum { RR_IDLE, RR_PENDING };
enum { RRM_GBA, RRM_NOSTORES, RRM_DIFFERS, RRM_HANDLER, RRM_INCOMPLETE, RRM_CONTROL, RRM_ROLLBACK, RRM_SLOTS,
       RRM_NONE, RRM_COUNT };
static const char *const kRrMarks[RRM_COUNT] = { "gba", "nostores", "differs", "handler", "incomplete", "control",
                                                 "rollback", "slots", "none" };

/* One global, all of it this process's own (savestate.py: SESSION); the big
   buffers are on the heap. */
static struct
{
    FILE *file;
    const char *path;
    uint32_t *index, blocks;           /* per block: tick, file offset */
    uint8_t *block;                    /* the tick's block */
    size_t blockCap;
    uint32_t crc, flags, regionCount, eventCount, storeCount;
    struct RrRegion region[RR_MAX_REGIONS];
    uint32_t regionStores[RR_MAX_REGIONS];
    const uint16_t *events;
    const struct RrStore *stores;
    uint8_t *skip;                     /* per store: 1 = left to the live line handlers (RrLoad) */
    size_t skipCap;
    int step, mark;                    /* mark: RRM_*, -1 none */
    char note[96];
    uint32_t hash;                     /* frame15_raster */
    uint32_t counts[RRM_COUNT];
    uint8_t *s0, *m1;                  /* the memory at the frame step's start, and after line 159 */
    uint16_t (*frame)[RENDER_FRAME_WIDTH], (*redrawn)[RENDER_FRAME_WIDTH];
    uint32_t *rgba, frameHash;
    int redrawing, apply, applyFail;
    uint32_t slot, nextStore, pcEventCount;
    uint16_t pcEvents[RR_MAX_EVENTS];
    void *realHandler[3];
} sRaster = { .mark = -1 };

static uint32_t Crc32(const uint8_t *p, size_t n);

static void OpenGbaRaster(const char *path)
{
    FILE *f = fopen(path, "rb");
    uint32_t head[4], cap = 0;
    off_t pos;

    if (f == NULL || fread(head, 4, 2, f) != 2 || head[0] != 0x31465252u /* "RRF1" */)
        Fail("not a raster log (MGBA_RUN_RASTERLOG): ", path);
    pos = 8;
    for (;;)
    {
        uint32_t n;

        if (fread(head, 4, 4, f) != 4)
            break;
        if (head[0] != 0x31425252u /* "RRB1" */ || head[3] > RR_MAX_REGIONS)
            Fail("raster log: a block is broken in ", path);
        if (sRaster.blocks > 0 && head[1] <= sRaster.index[2 * (sRaster.blocks - 1)])
            Fail("raster log: ticks out of order in ", path);
        if (sRaster.blocks == cap)
        {
            cap = cap ? 2 * cap : 1024;
            sRaster.index = realloc(sRaster.index, 2 * cap * sizeof(*sRaster.index));
            if (sRaster.index == NULL)
                Fail("out of memory for the raster log index", NULL);
        }
        sRaster.index[2 * sRaster.blocks] = head[1];
        sRaster.index[2 * sRaster.blocks + 1] = (uint32_t)pos;
        sRaster.blocks++;
        if (fseeko(f, (off_t)head[3] * 24, SEEK_CUR) != 0 || fread(&n, 4, 1, f) != 1
            || fseeko(f, (off_t)((n + 1) & ~1u) * 2, SEEK_CUR) != 0 || fread(&n, 4, 1, f) != 1
            || fseeko(f, (off_t)n * sizeof(struct RrStore), SEEK_CUR) != 0)
            Fail("raster log: a block is cut short in ", path);
        pos = ftello(f);
    }
    sRaster.file = f;
    sRaster.path = path;
    sRaster.s0 = malloc(RR_MEM_BYTES);
    sRaster.m1 = malloc(RR_MEM_BYTES);
    sRaster.frame = malloc(sizeof(gRenderFrame555));
    sRaster.redrawn = malloc(sizeof(gRenderFrame555));
    sRaster.rgba = malloc(sizeof(gRenderFrameRgba));
    if (sRaster.s0 == NULL || sRaster.m1 == NULL || sRaster.frame == NULL || sRaster.redrawn == NULL
        || sRaster.rgba == NULL)
        Fail("out of memory for the raster-timing redraw", NULL);
    PortLog("[gate0] raster replay: GBA raster log of %u frames from %s\n", sRaster.blocks, path);
}

/*
 * I/O regions (GBA_RASTER_RAM's REG_IE, BG scroll registers and, since round
 * 10, REG_DISPSTAT). The GBA's line
 * handlers write some of these registers themselves (HBlank_BgVerticalScroll,
 * WorldMapHblank, the phase intro's VCount handlers), and the redraw runs the
 * PC's handlers live, so an I/O halfword that a line handler stored in this
 * frame (store flag bit 0) is left to them, as an unwatched register is: none
 * of its stores in the frame is rolled back, made again or checked. The other
 * watched I/O halfwords, which only the main loop (OnMain, and OnVBlank's
 * flush) stored, are replayed like RAM. Per store, sRaster.skip[i].
 */
#define RR_IO_BASE 0x04000000u
#define RR_IO_END  0x04000208u /* the runner's limit: below REG_IME */

static int RrIsIo(uint32_t addr)
{
    return (addr >> 24) == 4;
}

/*
 * The bits of a watched halfword that the hardware keeps. mGBA's register
 * image holds a register as the hardware does (BGxHOFS/VOFS are 9 bits wide,
 * REG_IE 14), the PC's I/O image holds what the game wrote (port/hw keeps the
 * whole halfword; the renderer and Irq() use only those bits). So an I/O
 * value is compared through this mask; memory is compared whole. REG_DISPSTAT:
 * bits 3-15, what a store sets; bits 0-2 are the status flags, which the
 * runner leaves out of its log and port/hw/frame.c sets on every line.
 */
#define RR_REG_DISPSTAT 0x04000004u
#define RR_REG_IE       0x04000200u

static uint16_t RrBits(uint32_t addr)
{
    if (addr >= 0x04000010u && addr < 0x04000020u)
        return 0x01FF;
    if (addr == RR_REG_IE)
        return 0x3FFF;
    if (addr == RR_REG_DISPSTAT)
        return 0xFFF8;
    return 0xFFFF;
}

/* A replayed store of the GBA's value v at addr. REG_DISPSTAT keeps the PC's own status flags (bits 0-2, the line's
   state: the line handlers read them); everything else takes the GBA's halfword. */
static void RrPut(uint32_t addr, uint16_t v)
{
    volatile uint16_t *p = (volatile uint16_t *)(uintptr_t)addr;

    if (addr == RR_REG_DISPSTAT)
        *p = (uint16_t)((*p & ~RrBits(addr)) | (v & RrBits(addr)));
    else
        *p = v;
}

/* Does the GBA's store s turn an IRQ source on: a REG_IE bit, REG_DISPSTAT's VBlank, HBlank or VCount IRQ enable,
   or the VCount line moved while that IRQ is on? (The runner logs REG_DISPSTAT without its flags.) */
static int RrEnablesIrq(const struct RrStore *s)
{
    uint16_t on = (uint16_t)(s->newValue & ~s->oldValue);

    if (s->addr == RR_REG_IE)
        return (on & 0x3FFF) != 0;
    if (s->addr == RR_REG_DISPSTAT)
        return (on & 0x0038) != 0 || (((s->newValue ^ s->oldValue) & 0xFF00) != 0 && (s->newValue & 0x0020) != 0);
    return 0;
}

static void RrMarkHandlerIo(void)
{
    uint8_t owned[(RR_IO_END - RR_IO_BASE) / 2];
    uint32_t i;

    memset(owned, 0, sizeof(owned));
    for (i = 0; i < sRaster.storeCount; i++)
    {
        const struct RrStore *s = &sRaster.stores[i];

        if (!RrIsIo(s->addr))
            continue;
        if (s->addr < RR_IO_BASE || s->addr >= RR_IO_END)
            Fail("raster log: an I/O store at or above REG_IME in ", sRaster.path);
        if (s->flags & 1)
            owned[(s->addr - RR_IO_BASE) / 2] = 1;
    }
    for (i = 0; i < sRaster.storeCount; i++)
    {
        const struct RrStore *s = &sRaster.stores[i];

        sRaster.skip[i] = (uint8_t)(RrIsIo(s->addr) && owned[(s->addr - RR_IO_BASE) / 2]);
    }
}

/* The tick's block into sRaster (0: none). */
static int RrLoad(uint32_t tick)
{
    uint32_t lo = 0, hi = sRaster.blocks, i;
    off_t start, end;
    size_t size, off;

    while (lo < hi)
    {
        uint32_t mid = lo + (hi - lo) / 2;

        if (sRaster.index[2 * mid] < tick)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo == sRaster.blocks || sRaster.index[2 * lo] != tick)
        return 0;
    start = sRaster.index[2 * lo + 1];
    if (lo + 1 < sRaster.blocks)
        end = sRaster.index[2 * lo + 3];
    else if (fseeko(sRaster.file, 0, SEEK_END) != 0 || (end = ftello(sRaster.file)) < 0)
        Fail("cannot read ", sRaster.path);
    size = (size_t)(end - start);
    if (size > sRaster.blockCap)
    {
        sRaster.blockCap = size;
        sRaster.block = realloc(sRaster.block, size);
        if (sRaster.block == NULL)
            Fail("out of memory for the raster log", NULL);
    }
    if (fseeko(sRaster.file, start, SEEK_SET) != 0 || fread(sRaster.block, 1, size, sRaster.file) != size)
        Fail("cannot read ", sRaster.path);
    sRaster.crc = Crc32(sRaster.block, size); /* the runner's CRC-32 of the block, the GBA trace's "raster" */
    memcpy(&sRaster.flags, sRaster.block + 8, 4);
    memcpy(&sRaster.regionCount, sRaster.block + 12, 4);
    off = 16;
    for (i = 0; i < sRaster.regionCount; i++, off += 24)
    {
        struct RrRegion *r = &sRaster.region[i];

        memcpy(r->name, sRaster.block + off, 12);
        r->name[11] = '\0';
        memcpy(&r->addr, sRaster.block + off + 12, 4);
        memcpy(&r->size, sRaster.block + off + 16, 4);
        memcpy(&r->fnv, sRaster.block + off + 20, 4);
        sRaster.regionStores[i] = 0;
    }
    memcpy(&sRaster.eventCount, sRaster.block + off, 4);
    sRaster.events = (const uint16_t *)(const void *)(sRaster.block + off + 4);
    off += 4 + 2 * ((sRaster.eventCount + 1) & ~1u);
    memcpy(&sRaster.storeCount, sRaster.block + off, 4);
    sRaster.stores = (const struct RrStore *)(const void *)(sRaster.block + off + 4);
    if (off + 4 + (size_t)sRaster.storeCount * sizeof(struct RrStore) != size)
        Fail("raster log: a block's size does not add up in ", sRaster.path);
    if (sRaster.storeCount > sRaster.skipCap)
    {
        sRaster.skipCap = sRaster.storeCount;
        sRaster.skip = realloc(sRaster.skip, sRaster.skipCap);
        if (sRaster.skip == NULL)
            Fail("out of memory for the raster log", NULL);
    }
    RrMarkHandlerIo();
    for (i = 0; i < sRaster.storeCount; i++)
    {
        const struct RrStore *s = &sRaster.stores[i];
        uint32_t k;

        for (k = 0; k < sRaster.regionCount; k++)
            if (s->addr >= sRaster.region[k].addr && s->addr - sRaster.region[k].addr < sRaster.region[k].size)
                break;
        if (k == sRaster.regionCount || (s->addr & 1))
            Fail("raster log: a store outside the watched regions in ", sRaster.path);
        if (!sRaster.skip[i])
            sRaster.regionStores[k]++;
    }
    return 1;
}

/* Save (or load) the GBA memory the frame step can change. */
static void RrMem(uint8_t *buf, int save)
{
    size_t off = 0;
    unsigned i;

    for (i = 0; i < RR_MEM_PARTS; i++)
    {
        uint8_t *p = (uint8_t *)(uintptr_t)kRrMem[i][0];

        if (save)
            memcpy(buf + off, p, kRrMem[i][1]);
        else
            memcpy(p, buf + off, kRrMem[i][1]);
        off += kRrMem[i][1];
    }
}

/* Where address a is in a RrMem buffer. */
static size_t RrMemOffset(uint32_t a)
{
    size_t off = 0;
    unsigned i;

    for (i = 0; i < RR_MEM_PARTS; i++)
    {
        if (a >= kRrMem[i][0] && a - kRrMem[i][0] < kRrMem[i][1])
            return off + (a - kRrMem[i][0]);
        off += kRrMem[i][1];
    }
    Fail("raster replay: an address outside the memory it keeps", NULL);
    return 0;
}

static void RrMark(int mark, const char *note)
{
    sRaster.mark = mark;
    snprintf(sRaster.note, sizeof(sRaster.note), "%s", note != NULL ? note : "");
}

/* The GBA stores the redraw makes again (those not left to the line handlers). */
static uint32_t RrStoresToReplay(void)
{
    uint32_t i, n = 0;

    for (i = 0; i < sRaster.storeCount; i++)
        n += !sRaster.skip[i];
    return n;
}

/* After tick t's sample, before its frame step. */
static void RasterPrepare(uint32_t tick)
{
    sRaster.step = RR_IDLE;
    sRaster.mark = -1;
    sRaster.note[0] = '\0';
    if (sRaster.file == NULL || !IsShotTick(tick) || !gRenderEnabled)
        return;
    if (!RrLoad(tick))
    {
        RrMark(RRM_NONE, NULL);
        return;
    }
    if (sRaster.flags & 1)
        RrMark(RRM_INCOMPLETE, "the GBA's raster log does not account for every change of a watched region");
    else if (RrStoresToReplay() == 0)
        RrMark(RRM_NOSTORES, NULL);
    else
    {
        RrMem(sRaster.s0, 1); /* the frame step starts from here */
        sRaster.step = RR_PENDING;
    }
}

static int RasterRedrawing(void)
{
    return sRaster.redrawing;
}

/* The stores due before line event number upTo (all with pos <= upTo), in the GBA's order. holdIrqOn: stop at the
   first one that turns an IRQ source on (RrEnablesIrq), which then waits, with every store after it. */
static void RrApplyDueExt(uint32_t upTo, int holdIrqOn)
{
    while (sRaster.nextStore < sRaster.storeCount && sRaster.stores[sRaster.nextStore].pos <= upTo)
    {
        const struct RrStore *s = &sRaster.stores[sRaster.nextStore];
        volatile uint16_t *p = (volatile uint16_t *)(uintptr_t)s->addr;

        if (sRaster.skip[sRaster.nextStore])
        {
            sRaster.nextStore++;
            continue; /* a line handler's I/O register: the live handlers write it */
        }
        if (holdIrqOn && RrIsIo(s->addr) && RrEnablesIrq(s))
            return;
        sRaster.nextStore++;
        if (((*p ^ s->oldValue) & RrBits(s->addr)) != 0)
            sRaster.applyFail = 1; /* something else wrote the halfword in between */
        RrPut(s->addr, s->newValue);
    }
}

static void RrApplyDue(uint32_t upTo)
{
    RrApplyDueExt(upTo, 0);
}

/* A line event of the redraw: kind 1 VCount handler, 2 line drawn, 3 HBlank handler. */
static void RrEvent(int kind, int line)
{
    if (sRaster.pcEventCount < RR_MAX_EVENTS)
        sRaster.pcEvents[sRaster.pcEventCount] = (uint16_t)(kind << 8 | (line & 0xFF));
    sRaster.pcEventCount++;
    if (sRaster.apply)
        RrApplyDue(sRaster.slot);
    sRaster.slot++;
}

/*
 * port/hw/frame.c's IRQ checks (this binary only): Irq(n) before its IME and
 * IE test, and since round 10 also LineStart before its VCount test (n 2) and
 * LineHBlank before its REG_DISPSTAT HBlank test (n 1). During the GBA-timing
 * redraw, the stores due before the next line event. One of them can be the
 * GBA main loop's REG_IE write or OnVBlank's REG_DISPSTAT flush, so the test
 * that follows takes or refuses this IRQ as the GBA did at this point of the
 * frame. The GBA's log places a store between two line events only (no line
 * is drawn between them, so only these checks can tell when in that gap it
 * came), and the GBA took no IRQ in the gap:
 *   - a store that turns no IRQ source on is made here at the earliest moment
 *     the log allows (every line event before it has happened): so one that
 *     turns a handler off does so before the PC's next check (G7);
 *   - a store that turns one on (RrEnablesIrq) waits for the check of the
 *     GBA's next line event, the HBlank or VCount IRQ of that line (or for
 *     that event itself), so the redraw takes no IRQ in the gap either: the
 *     HBlank IRQ enable that the GBA's FlushLCDControl stored at line 182
 *     after a slow VBlank proc tree (C10-10 (b)), where the PC's flush was at
 *     line 160. The stores after it wait with it, in the GBA's order.
 * If the GBA's IRQs still differ from the redraw's (an IRQ taken on one side
 * only, or at another line, as with P-25's latched IF), the comparison of the
 * line events after the redraw reports the frame ("slots"), so nothing is
 * labelled on a guess.
 * n is the IRQ Irq (port/hw/frame.c) is about to check: any n but 2 (VCount)
 * is checked as an HBlank event (1). A VBlank (0) would be miscounted, but it
 * cannot come here during a redraw: the redraw skips line 160, so it never
 * calls Irq(0).
 */
void PortGate0IrqDue(int n)
{
    uint16_t check;

    if (!sRaster.redrawing || !sRaster.apply)
        return;
    check = (uint16_t)((n == 2 ? 1 : 3) << 8 | (*(volatile uint16_t *)(uintptr_t)0x04000006u & 0xFF)); /* REG_VCOUNT */
    RrApplyDueExt(sRaster.slot, !(sRaster.slot < sRaster.eventCount && sRaster.slot < RR_MAX_EVENTS
                                  && sRaster.events[sRaster.slot] == check));
}

/* The redraw sees the handler calls through these, in gIRQHandlers[1] and [2]. */
static void RrHBlank(void);
static void RrVCount(void);

static void RrInstall(void)
{
    if (gIRQHandlers[1] != (void *)RrHBlank)
    {
        sRaster.realHandler[1] = gIRQHandlers[1];
        gIRQHandlers[1] = (void *)RrHBlank;
    }
    if (gIRQHandlers[2] != (void *)RrVCount)
    {
        sRaster.realHandler[2] = gIRQHandlers[2];
        gIRQHandlers[2] = (void *)RrVCount;
    }
}

static void RrHandler(int n)
{
    void (*handler)(void) = (void (*)(void))sRaster.realHandler[n];

    RrEvent(n == 2 ? 1 : 3, *(volatile uint16_t *)(uintptr_t)0x04000006u); /* REG_VCOUNT */
    if (handler != NULL)
        handler();
    RrInstall(); /* a handler that set another one (SetInterrupt_LCDVCountMatch) */
}

static void RrHBlank(void)
{
    RrHandler(1);
}

static void RrVCount(void)
{
    RrHandler(2);
}

void __wrap_RenderLine(int line)
{
    if (sRaster.redrawing)
        RrEvent(2, line);
    __real_RenderLine(line);
}

/* Lines 161-227 and 0-159 again from the memory in place; the frame's hash. */
extern uint16_t gPortIfOnMainStart, gPortIfFrameStart; /* port/hw/frame.c */
extern uint16_t gPortOnMainIrqIoPre[0x200];
extern uint8_t gPortOnMainIrqIoMask[0x200];

static uint32_t RrRedraw(int apply)
{
    /* P-25: the interrupt flags the redrawn frame starts from. The control
       redraw is the frame step again (IF as it began); the GBA-timing redraw
       makes OnMain's REG_IE stores again, so it starts from IF as OnMain
       began: an IF bit latched while IE was off (taken by the PC's OnMain at
       its REG_IE store) is then taken where the GBA's store came. The frame
       step's I/O memory, IF in it, is put back after the redraws. */
    *(volatile uint16_t *)(uintptr_t)0x04000202u = apply ? gPortIfOnMainStart : gPortIfFrameStart; /* REG_IF */
    /* The I/O halfwords a handler taken inside the PC's OnMain changed go back
       to their values from before it (frame.c), since the redraw takes that
       call where the GBA took it. REG_IE, REG_IF and REG_IME stay as set. */
    if (apply)
    {
        int i;

        for (i = 0; i < 0x200; i++)
            if (gPortOnMainIrqIoMask[i] && i != 0x200 / 2 && i != 0x202 / 2 && i != 0x208 / 2)
                ((volatile uint16_t *)(uintptr_t)0x04000000u)[i] = gPortOnMainIrqIoPre[i];
    }
    sRaster.redrawing = 1;
    sRaster.apply = apply;
    sRaster.applyFail = 0;
    sRaster.slot = 0;
    sRaster.nextStore = 0;
    sRaster.pcEventCount = 0;
    sRaster.realHandler[1] = sRaster.realHandler[2] = NULL;
    RrInstall();
    sInVBlank++; /* the handlers make no main-loop events */
    PortRedrawFrameLines();
    sInVBlank--;
    sRaster.redrawing = 0;
    return RenderHashFrame555(&gRenderFrame555[0][0]);
}

/* Watched regions with GBA stores: equal to the m1 copy? An I/O region is compared on the halfwords the redraw
   replayed only (the others are the live handlers', and line 159's HBlank handler has run since m1). */
static int RrRegionsAsM1(void)
{
    uint32_t i;

    for (i = 0; i < sRaster.regionCount; i++)
        if (sRaster.regionStores[i] != 0 && !RrIsIo(sRaster.region[i].addr)
            && memcmp((const void *)(uintptr_t)sRaster.region[i].addr, sRaster.m1 + RrMemOffset(sRaster.region[i].addr),
                      sRaster.region[i].size) != 0)
            return 0;
    for (i = 0; i < sRaster.storeCount; i++)
    {
        uint32_t a = sRaster.stores[i].addr;
        uint16_t now, then;

        if (sRaster.skip[i] || !RrIsIo(a))
            continue;
        now = *(volatile uint16_t *)(uintptr_t)a;
        memcpy(&then, sRaster.m1 + RrMemOffset(a), 2);
        if (((now ^ then) & RrBits(a)) != 0)
            return 0;
    }
    return 1;
}

/* After line 159 of the frame step (frame of record done), before its HBlank IRQ. */
static void RasterEndFrame(void)
{
    uint32_t i, hc, hg;
    char note[96];

    if (sRaster.step != RR_PENDING)
        return;
    sRaster.step = RR_IDLE;
    for (i = 0; i < sRaster.regionCount; i++)
    {
        const struct RrRegion *r = &sRaster.region[i];
        size_t off = RrMemOffset(r->addr);

        if (sRaster.regionStores[i] == 0)
            continue;
        if (RrIsIo(r->addr))
            continue; /* per halfword, below */
        if (Fnv1a((const uint8_t *)(uintptr_t)r->addr, r->size) != r->fnv)
        {
            RrMark(RRM_DIFFERS, r->name);
            return;
        }
        if (memcmp((const void *)(uintptr_t)r->addr, sRaster.s0 + off, r->size) != 0)
        {
            RrMark(RRM_HANDLER, r->name);
            return;
        }
    }
    /* An I/O region's FNV-1a is the GBA's after line 159's HBlank handler, so it is not compared; each halfword
       the redraw replays must be unchanged by the PC's frame step (its value is checked against the GBA's last
       store below, in the rollback). */
    for (i = 0; i < sRaster.storeCount; i++)
    {
        const struct RrStore *s = &sRaster.stores[i];
        uint16_t now, then;

        if (sRaster.skip[i] || !RrIsIo(s->addr))
            continue;
        now = *(volatile uint16_t *)(uintptr_t)s->addr;
        memcpy(&then, sRaster.s0 + RrMemOffset(s->addr), 2);
        if (((now ^ then) & RrBits(s->addr)) != 0) /* REG_DISPSTAT's status flags are the frame step's own */
        {
            snprintf(note, sizeof(note), "I/O 0x%08X", s->addr);
            RrMark(RRM_HANDLER, note);
            return;
        }
    }
    RrMem(sRaster.m1, 1);
    memcpy(sRaster.frame, gRenderFrame555, sizeof(gRenderFrame555));
    memcpy(sRaster.rgba, gRenderFrameRgba, sizeof(gRenderFrameRgba));
    sRaster.frameHash = gRenderFrameHash;

    RrMem(sRaster.s0, 0);
    hc = RrRedraw(0);
    if (hc != sRaster.frameHash)
    {
        snprintf(note, sizeof(note), "redrawn %08x, frame %08x", hc, sRaster.frameHash);
        RrMark(RRM_CONTROL, note);
        goto restore;
    }
    RrMem(sRaster.s0, 0);
    for (i = sRaster.storeCount; i-- > 0;) /* back to the GBA's memory before the frame, newest first */
    {
        const struct RrStore *s = &sRaster.stores[i];
        volatile uint16_t *p = (volatile uint16_t *)(uintptr_t)s->addr;

        if (sRaster.skip[i])
            continue;
        if (((*p ^ s->newValue) & RrBits(s->addr)) != 0)
        {
            snprintf(note, sizeof(note), "0x%08X holds %04x, the GBA stored %04x", s->addr, *p, s->newValue);
            RrMark(RRM_ROLLBACK, note);
            goto restore;
        }
        RrPut(s->addr, s->oldValue);
    }
    hg = RrRedraw(1);
    RrApplyDue(0xFFFFu); /* the stores after the last line event */
    if (sRaster.applyFail || !RrRegionsAsM1())
    {
        RrMark(RRM_ROLLBACK, "the GBA's stores do not lead to the PC's memory at the end of the frame");
        goto restore;
    }
    if (sRaster.pcEventCount != sRaster.eventCount
        || memcmp(sRaster.pcEvents, sRaster.events, 2 * (sRaster.eventCount < RR_MAX_EVENTS
                                                         ? sRaster.eventCount : RR_MAX_EVENTS)) != 0)
    {
        uint32_t k, n = sRaster.pcEventCount < sRaster.eventCount ? sRaster.pcEventCount : sRaster.eventCount;

        if (n > RR_MAX_EVENTS)
            n = RR_MAX_EVENTS;
        for (k = 0; k < n && sRaster.pcEvents[k] == sRaster.events[k]; k++)
            ;
        snprintf(note, sizeof(note), "event %u: PC %s line %u, GBA %s line %u (PC %u events, GBA %u)", k,
            k < sRaster.pcEventCount ? (const char *[]){ "?", "vcount", "draw", "hblank" }[sRaster.pcEvents[k] >> 8 & 3]
                                     : "none",
            k < sRaster.pcEventCount ? sRaster.pcEvents[k] & 0xFF : 0,
            k < sRaster.eventCount ? (const char *[]){ "?", "vcount", "draw", "hblank" }[sRaster.events[k] >> 8 & 3]
                                   : "none",
            k < sRaster.eventCount ? sRaster.events[k] & 0xFF : 0, sRaster.pcEventCount, sRaster.eventCount);
        RrMark(RRM_SLOTS, note);
        goto restore;
    }
    sRaster.hash = hg;
    memcpy(sRaster.redrawn, gRenderFrame555, sizeof(gRenderFrame555));
    RrMark(RRM_GBA, NULL);
restore:
    RrMem(sRaster.m1, 0);
    memcpy(gRenderFrame555, sRaster.frame, sizeof(gRenderFrame555));
    memcpy(gRenderFrameRgba, sRaster.rgba, sizeof(gRenderFrameRgba));
    gRenderFrameHash = sRaster.frameHash;
}

/* Called from FinishSample on a frame15 tick. */
static void RasterFinishSample(uint32_t tick)
{
    if (sRaster.mark < 0)
        return;
    fprintf(sOut, ",\"raster\":\"%s\"", kRrMarks[sRaster.mark]);
    if (sRaster.mark != RRM_NONE)
        fprintf(sOut, ",\"raster_rec\":\"%08x\"", sRaster.crc);
    if (sRaster.mark == RRM_GBA)
    {
        fprintf(sOut, ",\"frame15_raster\":\"%08x\"", sRaster.hash);
        if (sShotDir != NULL)
        {
            char path[512];
            FILE *f;

            snprintf(path, sizeof(path), "%s/tick%06u.raster.555", sShotDir, tick);
            f = fopen(path, "wb");
            if (f == NULL)
                Fail("cannot open ", path);
            fwrite(sRaster.redrawn, 1, sizeof(gRenderFrame555), f);
            fclose(f);
        }
    }
    else if (sRaster.note[0] != '\0')
        fprintf(sOut, ",\"raster_note\":\"%s\"", sRaster.note);
    sRaster.counts[sRaster.mark]++;
    sRaster.mark = -1;
}

/*
 * Lag position (README.md, "Lag position"). The tick's main-loop events, in
 * the same order as the GBA capture's breakpoints:
 *   RunProcessRecursive entered; RunProcessRecursive returning; RunProcessScript
 *   entered (proc.c: the test build compiles proc.o with -finstrument-functions,
 *   port/test/pc/gate0.mk, so they arrive in __cyg_profile_func_enter/exit
 *   below); GetGameClock called; SetGameTime called (--wrap); FlipBgVerticalScroll
 *   called (--wrap, a line handler's hand-over, below); a BIOS copy or
 *   LZ77 call (CpuSet, CpuFastSet, LZ77UnCompWram, LZ77UnCompVram, --wrap;
 *   not RLUnComp* or HuffUnComp, which no route calls, as on the GBA side)
 *   whose destination is video memory (palette RAM, VRAM
 *   or OAM, 0x05000000-0x07FFFFFF), made while no game IRQ handler runs.
 * Events inside OnVBlank (its tree-0 Proc_Run, the clock reads of tree-0
 * procs) are not main-loop events, on either side. Every tick line has
 * "events", the count since the previous sample (tick 0: since power-on).
 * Counting is on whenever GATE0_OUT is set; it changes nothing in the run.
 */
static uint32_t sEvents;
static uint32_t sSndSince; /* this tick: the main loop's top-level m4a calls since the last event or the sample
                              (the lag replay's sound positions, below) */
static int sInVBlank;                   /* inside OnVBlank (the frame step's, or a replayed lag VBlank) */
static int sEventsOn = -1;              /* -1 not decided yet */
static const void *sFnRpr, *sFnRps;     /* RunProcessRecursive and RunProcessScript (static in proc.c) */
static const void *sFnTrackStop;        /* port/audio/m4a_1_pc.c TrackStop (the lag replay's sound positions) */
static void SndTrackStopEnter(void);
static uint32_t sExeCrc;                /* CRC-32 of this binary, for the trace header ("exe_crc32") */

static void LagReplayDue(void);
static void LagReplayHead(void);

/* CRC-32 (IEEE 802.3, as zlib.crc32), so the compare can tell that two PC
   traces come from the same binary, and that it is the --pc-elf it reads. */
static uint32_t Crc32(const uint8_t *p, size_t n)
{
    static uint32_t table[256];
    uint32_t c = 0xFFFFFFFFu;
    unsigned i, k;

    if (table[1] == 0)
        for (i = 0; i < 256; i++)
        {
            uint32_t v = i;
            for (k = 0; k < 8; k++)
                v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            table[i] = v;
        }
    while (n--)
        c = table[(c ^ *p++) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

/* The two proc.c functions are file-static, so their addresses come from this
   binary's own symbol table (they are FUNC symbols there, with -g or not). */
static void FindProcFunctions(void)
{
    static const char *const names[3] = { "RunProcessRecursive", "RunProcessScript", "TrackStop" };
    const void *found[3] = { NULL, NULL, NULL };
    const uint8_t *map;
    const Elf32_Ehdr *eh;
    const Elf32_Shdr *sh;
    struct stat st;
    unsigned i, j, k;
    int fd = open("/proc/self/exe", O_RDONLY);

    if (fd < 0 || fstat(fd, &st) != 0)
        Fail("cannot read /proc/self/exe", NULL);
    map = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED)
        Fail("cannot map /proc/self/exe", NULL);
    sExeCrc = Crc32(map, (size_t)st.st_size);
    eh = (const Elf32_Ehdr *)map;
    if (memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0 || eh->e_ident[EI_CLASS] != ELFCLASS32)
        Fail("/proc/self/exe is not a 32-bit ELF file", NULL);
    sh = (const Elf32_Shdr *)(map + eh->e_shoff);
    for (i = 0; i < eh->e_shnum; i++)
    {
        const Elf32_Sym *sym;
        const char *strtab;

        if (sh[i].sh_type != SHT_SYMTAB || sh[i].sh_link >= eh->e_shnum)
            continue;
        sym = (const Elf32_Sym *)(map + sh[i].sh_offset);
        strtab = (const char *)(map + sh[sh[i].sh_link].sh_offset);
        for (j = 0; j < sh[i].sh_size / sizeof(Elf32_Sym); j++)
            for (k = 0; k < COUNT(names); k++)
                if (ELF32_ST_TYPE(sym[j].st_info) == STT_FUNC && strcmp(strtab + sym[j].st_name, names[k]) == 0)
                {
                    if (found[k] != NULL && found[k] != (const void *)(uintptr_t)sym[j].st_value)
                        Fail("two functions named ", names[k]);
                    found[k] = (const void *)(uintptr_t)sym[j].st_value;
                }
    }
    munmap((void *)map, (size_t)st.st_size);
    for (k = 0; k < COUNT(names); k++)
        if (found[k] == NULL)
            Fail("no function symbol in this binary for ", names[k]);
    sFnRpr = found[0];
    sFnRps = found[1];
    sFnTrackStop = found[2];
}

static int EventsOn(void)
{
    if (sEventsOn < 0)
    {
        sEventsOn = getenv("GATE0_OUT") != NULL;
        if (sEventsOn)
            FindProcFunctions();
    }
    return sEventsOn;
}

static void MainEvent(void)
{
    if (sInVBlank)
        return;
    LagReplayDue(); /* a GBA lag VBlank that came before this event */
    sEvents++;
    sSndSince = 0; /* sound positions count m4a calls from this event on (LagReplaySndDue) */
}

/* -finstrument-functions, proc.o and port/audio/m4a_1_pc.o only (gate0.mk).
   RunProcessScript's entry hook runs before its body, so a GBA lag VBlank that
   came at the script's head (after the entry, before its first command:
   "lag_head") runs here (LagReplayHead); TrackStop's entry is a place inside
   an m4a call (SndTrackStopEnter). */
void __cyg_profile_func_enter(void *fn, void *site)
{
    (void)site;
    if (EventsOn() && (fn == sFnRpr || fn == sFnRps))
    {
        MainEvent();
        if (fn == sFnRps)
            LagReplayHead();
    }
    else if (fn == sFnTrackStop && sEventsOn > 0)
        SndTrackStopEnter();
}

void __cyg_profile_func_exit(void *fn, void *site)
{
    (void)site;
    if (EventsOn() && fn == sFnRpr)
        MainEvent();
}

uint32_t __wrap_GetGameClock(void)
{
    if (EventsOn())
        MainEvent();
    return __real_GetGameClock();
}

void __wrap_SetGameTime(int newTime)
{
    if (EventsOn())
        MainEvent();
    __real_SetGameTime(newTime);
}

/*
 * The BIOS copy and decompression calls (port/bios/bios.c, the GBA's SWI
 * wrappers in libagbsyscall; README.md, "Lag position", kind 6). One into
 * video memory is a main-loop event when no IRQ handler runs: OnVBlank's (the
 * flush and the tree-0 tasks such as BMapVSync_UpdateMapImgAnimations, which
 * copy into VRAM themselves) and the HBlank and VCount handlers' are not, as
 * on the GBA (gate0_capture.lua: no IRQ frame on the GBA's IRQ stack).
 *
 * Where a GBA lag VBlank can fall in such a call (mGBA 0.10.5's HLE BIOS):
 *   LZ77UnCompWram/Vram: the HLE in C writes all of the data at once, then the
 *     BIOS stall loop runs with IRQs on, so a lag VBlank in the call came after
 *     every write; the replay runs it before the next event, after the call.
 *   CpuSet/CpuFastSet: ARM copy loops in the BIOS image, interruptible between
 *     any two instructions, so a lag VBlank can come part way through. The GBA
 *     trace then has "lag_split" for it (bytes stored and source bytes loaded by
 *     then), and the replay splits the call there (LagSplitCall, below).
 * Returns 1 when the call was an event.
 */
static int LagSplitCall(int fast, const void *src, void *dest, uint32_t control);

static int BiosEvent(const void *dest)
{
    uintptr_t d = (uintptr_t)dest;

    if (EventsOn() && d >= PORT_PLTT_START && d < PORT_ROM_START && gPortIrqDepth == 0)
    {
        MainEvent();
        return !sInVBlank;
    }
    return 0;
}

void __wrap_CpuSet(const void *src, void *dest, uint32_t control)
{
    if (BiosEvent(dest) && LagSplitCall(0, src, dest, control))
        return;
    __real_CpuSet(src, dest, control);
}

void __wrap_CpuFastSet(const void *src, void *dest, uint32_t control)
{
    if (BiosEvent(dest) && LagSplitCall(1, src, dest, control))
        return;
    __real_CpuFastSet(src, dest, control);
}

void __wrap_LZ77UnCompWram(const void *src, void *dest)
{
    BiosEvent(dest);
    __real_LZ77UnCompWram(src, dest);
}

void __wrap_LZ77UnCompVram(const void *src, void *dest)
{
    BiosEvent(dest);
    __real_LZ77UnCompVram(src, dest);
}


/*
 * A line handler's hand-over (README.md, "Lag position", kind 7):
 * FlipBgVerticalScroll gives HBlank_BgVerticalScroll (bmlib-hblank.c) the next
 * frame's scroll table, which the handler takes at line 159 of every frame,
 * lag frames too (the lag replay runs their lines, below). As an event it
 * puts a lag VBlank before or after the flip as on the GBA: TAS route ticks
 * 525 and 549, where the flip and the lag VBlank are in one savedraw step.
 * The GBA counts it at the function's bx lr, after the flip's store, so a lag
 * VBlank inside the function came before it, and runs before the flip here.
 * Not resolved: the GBA takes the table at line 159's HBlank, about a line
 * before the VBlank, and the replay runs that take with the lag VBlank; a flip
 * between the two (the last line of a lag frame) is taken old on the GBA and
 * new here (README.md, "Lag position").
 */
void __wrap_FlipBgVerticalScroll(void)
{
    if (EventsOn() && gPortIrqDepth == 0)
        MainEvent();
    __real_FlipBgVerticalScroll();
}

/*
 * The lag replay (README.md, "Lag replay", the lag check): GATE0_LAG_FROM=GBA
 * trace (format 4). Each GBA lag VBlank of tick t is run on the PC at the
 * place it came on the GBA: before main-loop event k of tick t, where k is
 * its entry in the GBA tick's "lag_at" (the events before it), or at the
 * sample when k is the tick's last position. That run is the game's own
 * OnVBlank with gBmSt.main_loop_ended == 0, as on the GBA while the main loop
 * runs (bm.c:303-324: IncrementGameClock, the sound driver, Proc_Run of tree
 * 0, SyncLoOam, no flush). The clock is not touched: IncrementGameClock and
 * the game's SetGameTime calls, in the GBA's order, give the GBA's values.
 * Inner VBlankIntrWait VBlanks are not lag VBlanks (the GBA capture leaves
 * them out of lag_at) and run on the PC anyway.
 *
 * Each lag VBlank ends a frame the GBA drew while its main loop ran, and that
 * frame had its own line interrupts: the HBlank IRQ on every line and the
 * VCount IRQ, as in any frame. The PC steps one frame per tick, so the replay
 * runs those too, in the frame step's order (port/hw/frame.c,
 * PortLagFrameLines): lines 161-227 and 0-159 and line 160's VCount IRQ just
 * before the OnVBlank, line 160's HBlank IRQ just after it, nothing drawn.
 * A handler that takes main-loop state on a line (HBlank_BgVerticalScroll
 * takes the next frame's scroll table at line 159) then sees it as the GBA
 * did at that VBlank, to the event (TAS route tick 549, the save menu's fog).
 *
 * Each tick line of the replay has "lag_at" with the event count at which
 * each was run; the compare checks that it is the GBA's. If the PC tick has
 * fewer events than a GBA position, that VBlank runs at the sample, after
 * the tick's last event, and the tick's "lag_at" shows the smaller count.
 *
 * An early flush VBlank (a GBA tick with "early_flush":1, README.md, "Early
 * flush VBlank": OnVBlank after the main loop set gBmSt.main_loop_ended and
 * before its VBlankIntrWait; the last "lag_at" entry of that tick) runs at
 * the sample point too, always: after the tick's last event and the main
 * loop's own tail, with main_loop_ended as the main loop left it (1), so it
 * flushes and clears the flag as on the GBA. The tick is then sampled, as the
 * GBA's is at the wait that follows, and the frame step's OnVBlank (the
 * VBlank that wait waits for) finds the flag clear, as the GBA's does. The
 * replay's tick line then has "early_flush":1 too.
 */
static const char *sLagPath;
static uint32_t *sLagPos, *sLagFirst, *sGbaEvents; /* positions; tick t's are sLagPos[sLagFirst[t] ..] */
static uint8_t *sGbaEarly;                         /* per tick: 1 when its last lag_at entry is an early flush */
static uint32_t sLagTicks, sLagPosCount;
static uint32_t sLagDone;                          /* this tick's GBA lag VBlanks already run */
/* The most lag VBlanks one tick may have. 256 until coverage 12's item gallery (port/test/inputs/gallery/README.md):
   there each enemy phase starts with one main-loop pass of 380 to 1,685 VBlanks on the GBA. */
#define LAG_USED_MAX 4096
static uint32_t sLagUsed[LAG_USED_MAX], sLagUsedCount;      /* this tick: the event count at each run */
static int sEarlyRan;                              /* this tick: its early flush VBlank was run */
static uint32_t sLagRuns, sLagExact, sEarlyRuns, sEventMismatch, sEventMismatchTicks[8];
static uint32_t sLagOffTick; /* 1 + the first tick with a lag VBlank run off its GBA position (0: none) */
/* Lag VBlanks inside a CpuSet/CpuFastSet ("lag_split", LagSplitCall): the
   GBA's records, an input, and a log count (savestate.py SESSION). A lag
   VBlank run inside a split call has LAG_USED_SPLIT in its sLagUsed entry. */
static struct
{
    int32_t *written, *read; /* per GBA lag position: bytes stored and source bytes loaded by then; -1 none */
    uint32_t runs;
} sLagSplit;
#define LAG_USED_SPLIT 0x80000000u
/* Lag VBlanks at the head of a RunProcessScript ("lag_head", LagReplayHead):
   the GBA's records, an input, and a log count. A VBlank run there has
   LAG_USED_HEAD in its sLagUsed entry. */
static struct
{
    uint8_t *at; /* per GBA lag position: 1 when it came before the script's first command */
    uint32_t runs;
} sLagHead;
#define LAG_USED_HEAD 0x40000000u
/*
 * Sound positions (README.md, "Sound on any route"): a GBA trace captured with
 * the sound calls (gate0_capture.lua SND_CALLS; header "lag_snd":1) gives, per
 * lag VBlank, the main loop's top-level m4a calls since the event its lag_at
 * entry counts to ("lag_snd"). An m4a call is no main-loop event, so lag_at
 * alone runs such a VBlank before the next event, after any m4a call made in
 * between, where the GBA may have had it before the call (route_ch1 tick 754:
 * m4aSongNumStart after three lag VBlanks on the GBA, before them here). The
 * replay then also runs a lag VBlank at the main loop's next top-level m4a
 * call once that many calls were made since its event (the m4a wraps below,
 * LagReplaySndDue), and the replay's line gets "lag_snd" with the count each
 * was run at. The count rides in bits 20-29 of its sLagUsed entry (the event
 * count in bits 0-19; bit 30 is LAG_USED_HEAD and bit 31 LAG_USED_SPLIT), so a
 * save state holds it with no new state global.
 * Inputs (sLagSnd, sLagSndOn) and the running count (sSndSince: calls since the
 * last event or the sample; sSndDepth: watched m4a calls open, 0 at the sample
 * point) are this run's own (savestate.py SESSION). A resumed replay starts
 * sSndSince at 0: only the "lag_snd" of the snapshot tick's own end-of-tick
 * lag VBlanks can show 0 where the full run had a count, never where any runs.
 *
 * A GBA lag VBlank can come inside the main loop's m4a call ("lag_snd_split":
 * [i, stops, locks], its lag_snd counting that call): stops is how many
 * TrackStop calls that call had made, locks what the VBlank handler found
 * locked (bit k: player k of gMPlayTable's ident is not ID_NUMBER; bits 16-23:
 * gSoundInfo.ident - ID_NUMBER). The handler's work depends on those: a
 * locked player is not played, nor the players after it in MPlayMain's
 * chain, a locked SoundInfo stops SoundMain, and only TrackStop changes the
 * channels the mixer plays (route_ch5 tick 102,053: the lag VBlank came
 * inside m4aSongNumStart(0x5A) with its player locked, so the music player
 * missed one step on the GBA). The replay runs such a VBlank at the first
 * place in that call where it finds the same: before the call (no lock yet,
 * stops 0), at the entry of TrackStop number stops + 1 (proc.o and
 * m4a_1_pc.o are compiled with -finstrument-functions in this build,
 * port/test/pc/gate0.mk), or at the call's return with the GBA's locks set
 * for the run of that VBlank and put back after it (the call's writes after
 * the place the GBA was at only touch its locked player, which the handler
 * does not play). Its line then has "lag_snd_split" with what it ran at; a
 * place it could not match is a lag VBlank off the GBA's position.
 *
 * A GBA lag VBlank can also come inside an IRQ handler's m4a call
 * ("lag_snd_nested": [i, locks]): an OnVBlank that ran into the next frame,
 * the next VBlank's IRQ taken while its m4aSoundMain still mixed (IrqMain
 * runs every handler with IRQs on; route_e15 tick 1,301). That VBlank's own
 * m4aSoundMain then finds SoundMain locked and mixes nothing (nor does its
 * MusicPlayer step run), while its m4aSoundVSync runs as usual. The replay
 * cannot run it inside the PC's m4aSoundMain, so it runs it at its lag_at
 * place with the GBA's locks set for the whole lag frame and put back after
 * it (SndForceLocks): the outer mix had read its DMA position before the
 * GBA's mixing loop (SoundMain reads pcmDmaCounter before it jumps to
 * SoundMainRAM, the long part) and gMaxLines is 0 (no deadline), so what
 * is left of it and the nested VBlank touch different state. Its line then
 * has "lag_snd_nested" with the locks it ran with.
 */
#define LAG_USED_EVENTS 0x000FFFFFu
#define LAG_USED_SND_SHIFT 20
#define LAG_USED_SND_MAX 0x3FFu /* bits 20-29: below LAG_USED_HEAD */
#define SND_ID_NUMBER 0x68736D53u
static int32_t *sLagSnd;  /* per GBA lag position: its lag_snd; NULL without */
static int32_t *sLagSndStops, *sLagSndLocks; /* per GBA lag position: its lag_snd_split; -1 none */
static int32_t *sLagSndNested; /* per GBA lag position: the locks of its lag_snd_nested; -1 none */
static int sLagSndOn;     /* the GBA trace has sound positions */
static int sSndDepth;     /* watched m4a calls open (the m4a wraps below) */
static int sSndMainOpen;  /* the open top-level call is the main loop's (counted in sSndSince) */
static uint32_t sSndStops; /* TrackStop calls made by it so far */
static int32_t sRunSnd = -1, sRunStops = -1, sRunLocks = -1; /* RunLagVBlank: a split run's place */
static int32_t sLagUsedStops[LAG_USED_MAX], sLagUsedLocks[LAG_USED_MAX]; /* this tick: each run's split place; -1 none */
static int32_t sLagUsedNested[LAG_USED_MAX]; /* this tick: the locks each run was nested with (lag_snd_nested); -1 none */

static void LagGrow(uint32_t *haveTicks, const char *path)
{
    if (sLagTicks + 2 > *haveTicks)
    {
        *haveTicks = *haveTicks ? *haveTicks * 2 : 65536;
        while (sLagTicks + 2 > *haveTicks)
            *haveTicks *= 2;
        sLagFirst = realloc(sLagFirst, *haveTicks * sizeof(*sLagFirst));
        sGbaEvents = realloc(sGbaEvents, *haveTicks * sizeof(*sGbaEvents));
        sGbaEarly = realloc(sGbaEarly, *haveTicks * sizeof(*sGbaEarly));
        if (sLagFirst == NULL || sGbaEvents == NULL || sGbaEarly == NULL)
            Fail("out of memory reading ", path);
    }
}

static void ReadGbaLag(const char *path)
{
    FILE *f = fopen(path, "r");
    char *line = NULL;
    size_t cap = 0;
    uint32_t haveTicks = 0, havePos = 0;
    int header = 1;

    if (f == NULL)
        Fail("cannot open ", path);
    while (getline(&line, &cap, f) > 0)
    {
        const char *s;

        if (header)
        {
            if (strstr(line, "\"source\":\"gba\"") == NULL || strstr(line, "\"format\":\"fe8-gate0-trace-4\"") == NULL)
                Fail("not a GBA trace in format fe8-gate0-trace-4 (recapture it): ", path);
            sLagSndOn = strstr(line, "\"lag_snd\":1") != NULL; /* sound positions (above) */
            header = 0;
            continue;
        }
        s = strstr(line, "\"tick\":");
        /* A resumed GBA trace (README.md, "Save states") starts at its prologue
           tick T-1: the ticks before it get no lag VBlanks here. */
        if (s != NULL && sLagTicks == 0 && PortSnap_TraceBase() != 0
            && strtoul(s + 7, NULL, 10) == PortSnap_TraceBase())
            for (; sLagTicks < PortSnap_TraceBase(); sLagTicks++)
            {
                LagGrow(&haveTicks, path);
                sLagFirst[sLagTicks] = 0;
                sGbaEvents[sLagTicks] = 0;
                sGbaEarly[sLagTicks] = 0;
            }
        if (s == NULL || strtoul(s + 7, NULL, 10) != sLagTicks)
            Fail("tick numbering broken in ", path);
        LagGrow(&haveTicks, path);
        sLagFirst[sLagTicks] = sLagPosCount;
        s = strstr(line, "\"events\":");
        if (s == NULL)
            Fail("a tick without \"events\" in ", path);
        sGbaEvents[sLagTicks] = (uint32_t)strtoul(s + 9, NULL, 10);
        s = strstr(line, "\"lag_at\":[");
        if (s != NULL)
            for (s += 10; *s != ']';)
            {
                char *end;
                unsigned long k = strtoul(s, &end, 10);

                if (end == s)
                    Fail("bad lag_at in ", path);
                if (sLagPosCount == havePos)
                {
                    havePos = havePos ? havePos * 2 : 1024;
                    sLagPos = realloc(sLagPos, havePos * sizeof(*sLagPos));
                    sLagSplit.written = realloc(sLagSplit.written, havePos * sizeof(*sLagSplit.written));
                    sLagSplit.read = realloc(sLagSplit.read, havePos * sizeof(*sLagSplit.read));
                    sLagHead.at = realloc(sLagHead.at, havePos * sizeof(*sLagHead.at));
                    sLagSnd = realloc(sLagSnd, havePos * sizeof(*sLagSnd));
                    sLagSndStops = realloc(sLagSndStops, havePos * sizeof(*sLagSndStops));
                    sLagSndLocks = realloc(sLagSndLocks, havePos * sizeof(*sLagSndLocks));
                    sLagSndNested = realloc(sLagSndNested, havePos * sizeof(*sLagSndNested));
                    if (sLagPos == NULL || sLagSplit.written == NULL || sLagSplit.read == NULL || sLagHead.at == NULL
                        || sLagSnd == NULL || sLagSndStops == NULL || sLagSndLocks == NULL || sLagSndNested == NULL)
                        Fail("out of memory reading ", path);
                }
                sLagSplit.written[sLagPosCount] = sLagSplit.read[sLagPosCount] = -1;
                sLagHead.at[sLagPosCount] = 0;
                sLagSnd[sLagPosCount] = sLagSndStops[sLagPosCount] = sLagSndLocks[sLagPosCount] = -1;
                sLagSndNested[sLagPosCount] = -1;
                sLagPos[sLagPosCount++] = (uint32_t)k;
                s = end + (*end == ',');
            }
        /* "lag_snd":[j,...]: sound positions, one per lag_at entry (above). */
        s = strstr(line, "\"lag_snd\":[");
        if (s != NULL || (sLagSndOn && sLagPosCount != sLagFirst[sLagTicks]))
        {
            uint32_t i = sLagFirst[sLagTicks];

            if (s == NULL || !sLagSndOn)
                Fail(s == NULL ? "a tick with lag_at and no lag_snd in a trace with sound positions: "
                               : "lag_snd in a trace whose header has no \"lag_snd\":1: ", path);
            for (s += 11; *s != ']'; i++)
            {
                char *end;
                long j = strtol(s, &end, 10);

                if (end == s || i >= sLagPosCount || j < 0 || j > (long)LAG_USED_SND_MAX)
                    Fail("bad lag_snd in ", path);
                sLagSnd[i] = (int32_t)j;
                s = end + (*end == ',');
            }
            if (i != sLagPosCount)
                Fail("lag_snd and lag_at of a tick differ in length in ", path);
        }
        /* "lag_snd_split":[[i,stops,locks],...]: lag VBlank i came inside an m4a call (above). */
        s = strstr(line, "\"lag_snd_split\":[");
        if (s != NULL)
            for (s += 17; *s == '[';)
            {
                char *end;
                unsigned long i = strtoul(s + 1, &end, 10), st, lk;

                if (*end != ',' || !sLagSndOn)
                    Fail("bad lag_snd_split in ", path);
                st = strtoul(end + 1, &end, 10);
                if (*end != ',')
                    Fail("bad lag_snd_split in ", path);
                lk = strtoul(end + 1, &end, 10);
                if (*end != ']' || sLagFirst[sLagTicks] + i >= sLagPosCount || st > 0x7FFFFFFFul || lk > 0x7FFFFFFFul)
                    Fail("bad lag_snd_split in ", path);
                sLagSndStops[sLagFirst[sLagTicks] + i] = (int32_t)st;
                sLagSndLocks[sLagFirst[sLagTicks] + i] = (int32_t)lk;
                s = end + 1;
                s += *s == ',';
            }
        /* "lag_snd_nested":[[i,locks],...]: lag VBlank i came inside an IRQ handler's m4a call (above). */
        s = strstr(line, "\"lag_snd_nested\":[");
        if (s != NULL)
            for (s += 18; *s == '[';)
            {
                char *end;
                unsigned long i = strtoul(s + 1, &end, 10), lk;

                if (*end != ',' || !sLagSndOn)
                    Fail("bad lag_snd_nested in ", path);
                lk = strtoul(end + 1, &end, 10);
                if (*end != ']' || sLagFirst[sLagTicks] + i >= sLagPosCount || lk > 0x7FFFFFFFul)
                    Fail("bad lag_snd_nested in ", path);
                sLagSndNested[sLagFirst[sLagTicks] + i] = (int32_t)lk;
                s = end + 1;
                s += *s == ',';
            }
        /* "lag_split":[[i,written,read],...]: lag VBlank i came inside a CpuSet/CpuFastSet. */
        s = strstr(line, "\"lag_split\":[");
        if (s != NULL)
            for (s += 13; *s == '[';)
            {
                char *end;
                unsigned long i = strtoul(s + 1, &end, 10), w, r;

                if (*end != ',')
                    Fail("bad lag_split in ", path);
                w = strtoul(end + 1, &end, 10);
                if (*end != ',')
                    Fail("bad lag_split in ", path);
                r = strtoul(end + 1, &end, 10);
                if (*end != ']' || sLagFirst[sLagTicks] + i >= sLagPosCount)
                    Fail("bad lag_split in ", path);
                sLagSplit.written[sLagFirst[sLagTicks] + i] = (int32_t)w;
                sLagSplit.read[sLagFirst[sLagTicks] + i] = (int32_t)r;
                s = end + 1;
                s += *s == ',';
            }
        /* "lag_head":[i,...]: lag VBlank i came at a RunProcessScript's head, before its first command. */
        s = strstr(line, "\"lag_head\":[");
        if (s != NULL)
            for (s += 12; *s != ']';)
            {
                char *end;
                unsigned long i = strtoul(s, &end, 10);

                if (end == s || (*end != ',' && *end != ']') || sLagFirst[sLagTicks] + i >= sLagPosCount)
                    Fail("bad lag_head in ", path);
                sLagHead.at[sLagFirst[sLagTicks] + i] = 1;
                s = end + (*end == ',');
            }
        s = strstr(line, "\"early_flush\":");
        sGbaEarly[sLagTicks] = s != NULL && strtoul(s + 14, NULL, 10) != 0;
        if (sGbaEarly[sLagTicks] && sLagPosCount == sLagFirst[sLagTicks])
            Fail("a tick with \"early_flush\" and no \"lag_at\" in ", path);
        sLagTicks++;
    }
    free(line);
    fclose(f);
    if (sLagTicks == 0)
        Fail("no ticks in ", path);
    sLagFirst[sLagTicks] = sLagPosCount;
    sLagPath = path;
    PortLog("[gate0] lag replay: %u GBA lag VBlanks in %u ticks from %s\n", sLagPosCount, sLagTicks, path);
}

static uint32_t LagCount(uint32_t tick)
{
    return tick < sLagTicks ? sLagFirst[tick + 1] - sLagFirst[tick] : 0;
}

/* The tick's lag VBlanks that came while the main loop ran (all but an early flush). */
static uint32_t LagCountInLoop(uint32_t tick)
{
    return LagCount(tick) - (tick < sLagTicks ? sGbaEarly[tick] : 0);
}

static int32_t SndLocks(void);
static void SndForceLocks(uint32_t want, uint32_t saved[17]);
static void SndRestoreLocks(const uint32_t saved[17]);

/* One GBA lag VBlank: OnVBlank while the main loop runs (earlyFlush 0), or
   the early flush VBlank, at the sample point (earlyFlush 1). One that came
   inside an IRQ handler's m4a call on the GBA (lag_snd_nested, above) runs
   with the GBA's locks set for its whole frame. */
static void RunLagVBlank(int earlyFlush)
{
    uint8_t ended = gBmSt[0];

    uint32_t pos = sLagFirst[sTick] + sLagDone;
    int32_t nested = sLagSndOn && sLagSndNested != NULL ? sLagSndNested[pos] : -1;
    uint32_t savedLocks[17];
    /* The sound place it runs at: the m4a calls since the event (a split run
       inside a call: the GBA's count, which counts that call, sRunSnd). */
    uint32_t snd = sRunSnd >= 0 ? (uint32_t)sRunSnd : sSndSince;

    if (sLagUsedCount == COUNT(sLagUsed))
        Fail("more lag VBlanks in one tick than the hook keeps", NULL);
    if (sEvents > LAG_USED_EVENTS || snd > LAG_USED_SND_MAX)
        Fail("a tick with more main-loop events or m4a calls than sLagUsed holds", NULL);
    if (nested >= 0)
        SndForceLocks((uint32_t)nested, savedLocks);
    sLagUsedStops[sLagUsedCount] = sRunStops;
    sLagUsedLocks[sLagUsedCount] = sRunLocks;
    sLagUsedNested[sLagUsedCount] = nested >= 0 ? SndLocks() : -1;
    sLagUsed[sLagUsedCount++] = sEvents | (snd << LAG_USED_SND_SHIFT);
    if (sEvents == sLagPos[pos] && (!sLagSndOn || sLagSnd[pos] == (int32_t)snd)
        && (!sLagSndOn || (sLagSndStops[pos] == sRunStops && sLagSndLocks[pos] == sRunLocks))
        && (!sLagSndOn || sLagSndNested[pos] == sLagUsedNested[sLagUsedCount - 1]))
        sLagExact++;
    else if (sLagOffTick == 0)
        sLagOffTick = sTick + 1; /* for a save state's record of the ticks before it (savestate.py lag_prefix) */
    sRunSnd = sRunStops = sRunLocks = -1;
    if (!earlyFlush)
        gBmSt[0] = 0; /* gBmSt.main_loop_ended: set at the sample, not yet when the GBA's VBlank came */
    else if (ended == 0)
        Fail("early flush VBlank: gBmSt.main_loop_ended is not set at the sample", NULL);
    sInVBlank++; /* the line handlers make no main-loop events (event counts equal the GBA's) */
    PortLagFrameLines(0); /* that frame's lines, up to line 160's VBlank */
    sInVBlank--;
    sOnVBlankCalls++;
    sInVBlank++;
    __real_OnVBlank(); /* an early flush: the flag is set, so this flushes and clears it */
    PortLagFrameLines(1); /* line 160's HBlank */
    sInVBlank--;
    if (nested >= 0)
        SndRestoreLocks(savedLocks);
    if (!earlyFlush)
        gBmSt[0] = ended;
    else
    {
        sEarlyRan = 1;
        sEarlyRuns++;
    }
    sLagDone++;
    sLagRuns++;
}

/*
 * A CpuSet or CpuFastSet into video memory that the GBA's lag VBlank came
 * inside ("lag_split" of the GBA trace; README.md, "Lag position", kind 6).
 * mGBA's HLE BIOS runs these as ARM loops (BIOS 0x214 and 0x2A8): CpuFastSet
 * loads 8 words (ldm) and then stores them (stm), CpuSet one unit at a time
 * (ldr or ldrh, then str or strh), and a fill loads its value once before its
 * loop. The GBA record says how many bytes were stored and how many source
 * bytes were loaded when the VBlank came; the call is done here in the same
 * three parts: the stored part, the lag VBlank (RunLagVBlank, at this event,
 * as the GBA's "lag_at" says), then the loaded-but-not-stored unit and the
 * rest. The loaded part is read before the VBlank, the rest after, as on the
 * GBA. Returns 1 when it made the call.
 */
static int LagSplitCall(int fast, const void *src, void *dest, uint32_t control)
{
    void (*copy)(const void *, void *, uint32_t) = fast ? __real_CpuFastSet : __real_CpuSet;
    uint32_t pos, w, r, unit, block, total, base, value = 0, held[8], i;
    const uint8_t *s;
    uint8_t *d;
    int fill;

    if (sLagFirst == NULL || sLagSplit.written == NULL || sTick >= sLagTicks || sLagDone >= LagCountInLoop(sTick))
        return 0;
    pos = sLagFirst[sTick] + sLagDone;
    if (sLagPos[pos] != sEvents || sLagSplit.written[pos] < 0)
        return 0;
    w = (uint32_t)sLagSplit.written[pos];
    r = (uint32_t)sLagSplit.read[pos];
    fill = (control & 0x01000000u) != 0;
    unit = fast || (control & 0x04000000u) ? 4 : 2;
    block = fast ? 32 : unit; /* what one store instruction writes */
    total = fast ? (((control & 0x1FFFFFu) + 7) & ~7u) * 4 : (control & 0x1FFFFFu) * unit;
    if (w > total || w % block || (fill ? (r != 0 && r != unit) : (r < w || r > w + block || r % unit || r > total)))
        Fail("lag replay: a lag_split record does not fit its BIOS call", NULL);
    base = control & ~0x1FFFFFu;
    s = (const uint8_t *)((uintptr_t)src & ~(uintptr_t)(unit - 1));
    d = (uint8_t *)((uintptr_t)dest & ~(uintptr_t)(unit - 1));
    if (fill && r != 0)
        value = unit == 4 ? *(const volatile uint32_t *)s : *(const volatile uint16_t *)s; /* loaded before */
    if (w != 0)
        copy(src, dest, base | (w / unit));
    if (!fill && r > w)
        memcpy(held, s + w, r - w); /* loaded, not stored yet */
    RunLagVBlank(0);
    sLagUsed[sLagUsedCount - 1] |= LAG_USED_SPLIT; /* this tick's "lag_split" (SampleEvents) */
    if (fill)
    {
        if (r == 0)
            value = unit == 4 ? *(const volatile uint32_t *)s : *(const volatile uint16_t *)s; /* loaded after */
        if (total > w)
            copy(&value, d + w, base | ((total - w) / unit));
    }
    else
    {
        for (i = 0; i < (r - w) / unit; i++)
            if (unit == 4)
                ((volatile uint32_t *)(void *)(d + w))[i] = held[i];
            else
                ((volatile uint16_t *)(void *)(d + w))[i] = ((const uint16_t *)held)[i];
        if (total > r)
            copy(s + r, d + r, base | ((total - r) / unit));
    }
    sLagSplit.runs++;
    return 1;
}

/* Before each main-loop event: the lag VBlanks that came before it. */
static void LagReplayDue(void)
{
    if (sLagFirst == NULL)
        return;
    while (sLagDone < LagCountInLoop(sTick) && sLagPos[sLagFirst[sTick] + sLagDone] <= sEvents)
        RunLagVBlank(0);
}

/*
 * At RunProcessScript's entry, after its event (README.md, "Lag position",
 * kind 3, "At a script's head"): the tick's next GBA lag VBlank, when the GBA
 * trace says it came there ("lag_head": after that RunProcessScript's entry,
 * before its first command; the capture checks that its tree-0 walk did not
 * change what RunProcessScript's checks read). Run here, before the script's
 * first command, instead of before the next event: cov9 A2, Ch2's Supply
 * screen, where Proc_Start's script of ProcScr_SlidingWallBg (tree 0) had not
 * run its first command, so the VBlank's tree-0 walk skipped the new proc
 * (PROC_FLAG_STARTING); before the next event Proc_Start has returned and the
 * walk runs the proc's loop once more. A head VBlank whose event k here is not
 * a RunProcessScript waits for the next event, as any other, and the replay's
 * "lag_head" then lacks it (the compare reports the tick).
 */
static void LagReplayHead(void)
{
    uint32_t pos;

    if (sLagFirst == NULL || sLagHead.at == NULL || sInVBlank || sTick >= sLagTicks
        || sLagDone >= LagCountInLoop(sTick))
        return;
    pos = sLagFirst[sTick] + sLagDone;
    if (!sLagHead.at[pos] || sLagPos[pos] != sEvents)
        return;
    RunLagVBlank(0);
    sLagUsed[sLagUsedCount - 1] |= LAG_USED_HEAD; /* this tick's "lag_head" (SampleEvents) */
    sLagHead.runs++;
}

/* The driver's locks as the GBA capture writes them (lag_snd_split, above). */
extern const uint8_t gMPlayTable[]; /* const struct MusicPlayer[9] (src/m4a_tables.c): info pointer first, 12 bytes */
extern char gNumMusicPlayers[];     /* ldscript absolute: the address is the count (as port/audio/fifo_pc.c) */
#define SND_PLAYER_IDENT 0x34       /* MusicPlayerInfo.ident */
#define SND_SOUNDINFO 0x03005410u   /* gSoundInfo (ident first) */

static volatile uint32_t *SndPlayerIdent(unsigned k)
{
    uint32_t info;

    memcpy(&info, gMPlayTable + 12 * k, 4);
    return (volatile uint32_t *)(uintptr_t)(info + SND_PLAYER_IDENT);
}

static int32_t SndLocks(void)
{
    unsigned k, n = (unsigned)(uintptr_t)gNumMusicPlayers;
    uint32_t m = 0;

    for (k = 0; k < n && k < 16; k++)
        if (*SndPlayerIdent(k) != SND_ID_NUMBER)
            m |= 1u << k;
    return (int32_t)(m | (((*(volatile uint32_t *)(uintptr_t)SND_SOUNDINFO - SND_ID_NUMBER) & 0xFFu) << 16));
}

/* The driver's locks set as the GBA's handler found them (want: SndLocks()'s
   bits: player k locked, SoundInfo's ident - ID_NUMBER in bits 16-23), the
   old idents kept in saved; SndRestoreLocks puts them back. A player that is
   already locked keeps its ident. */
static void SndForceLocks(uint32_t want, uint32_t saved[17])
{
    uint32_t k, n = (unsigned)(uintptr_t)gNumMusicPlayers;
    volatile uint32_t *si = (volatile uint32_t *)(uintptr_t)SND_SOUNDINFO;

    for (k = 0; k < n && k < 16; k++)
    {
        saved[k] = *SndPlayerIdent(k);
        if (want & (1u << k))
            *SndPlayerIdent(k) = saved[k] == SND_ID_NUMBER ? SND_ID_NUMBER + 1 : saved[k];
    }
    saved[16] = *si;
    if ((want >> 16) & 0xFFu)
        *si = SND_ID_NUMBER + ((want >> 16) & 0xFFu);
}

static void SndRestoreLocks(const uint32_t saved[17])
{
    uint32_t k, n = (unsigned)(uintptr_t)gNumMusicPlayers;

    for (k = 0; k < n && k < 16; k++)
        *SndPlayerIdent(k) = saved[k];
    *(volatile uint32_t *)(uintptr_t)SND_SOUNDINFO = saved[16];
}

/* The next lag VBlank of this tick, if its GBA place is inside the main loop's
   call that is open now (lag_snd_split, its lag_snd that call's count). */
static int SplitPending(uint32_t *pos)
{
    if (sLagFirst == NULL || !sLagSndOn || sTick >= sLagTicks || sLagDone >= LagCountInLoop(sTick))
        return 0;
    *pos = sLagFirst[sTick] + sLagDone;
    return sLagPos[*pos] == sEvents && sLagSndStops[*pos] >= 0 && sLagSnd[*pos] == (int32_t)sSndSince;
}

/* Before each top-level m4a call of the main loop (sound positions, above):
   the lag VBlanks that came after this event and after the calls made since
   it, before this one; and one the GBA had inside this call while it held no
   lock and had stopped no track yet (the call had changed nothing then). */
static void LagReplaySndDue(void)
{
    if (sLagFirst == NULL || !sLagSndOn || sTick >= sLagTicks)
        return;
    while (sLagDone < LagCountInLoop(sTick))
    {
        uint32_t pos = sLagFirst[sTick] + sLagDone;

        if (sLagPos[pos] > sEvents)
            break;
        if (sLagPos[pos] == sEvents && sLagSndStops[pos] >= 0 && sLagSnd[pos] == (int32_t)sSndSince + 1)
        {
            int32_t locks = SndLocks();

            if (sLagSndStops[pos] != 0 || sLagSndLocks[pos] != locks)
                break; /* further into this call */
            sRunSnd = (int32_t)sSndSince + 1, sRunStops = 0, sRunLocks = locks;
            RunLagVBlank(0);
            continue;
        }
        if (sLagPos[pos] == sEvents && sLagSnd[pos] > (int32_t)sSndSince)
            break;
        RunLagVBlank(0);
    }
}

/* At the entry of TrackStop made by the main loop's open call (the
   -finstrument-functions hook below): a GBA lag VBlank that came after that
   call's TrackStop calls so far, with the locks found here. */
static void SndTrackStopEnter(void)
{
    uint32_t pos;

    if (!sSndMainOpen || gPortIrqDepth != 0 || sInVBlank)
        return;
    while (SplitPending(&pos) && sLagSndStops[pos] == (int32_t)sSndStops && sLagSndLocks[pos] == SndLocks())
    {
        sRunSnd = (int32_t)sSndSince, sRunStops = (int32_t)sSndStops, sRunLocks = SndLocks();
        RunLagVBlank(0);
    }
    sSndStops++;
}

/* When the main loop's top-level call returns: a GBA lag VBlank that came
   inside it and has not run is run here with the GBA's locks set (and put
   back after it): what is left of the call at the GBA's place only writes the
   locked player's own state, which the handler does not play. */
static void LagReplaySplitExit(void)
{
    uint32_t pos;

    while (SplitPending(&pos))
    {
        uint32_t saved[17];

        SndForceLocks((uint32_t)sLagSndLocks[pos], saved);
        sRunSnd = (int32_t)sSndSince, sRunStops = (int32_t)sSndStops, sRunLocks = SndLocks();
        RunLagVBlank(0);
        SndRestoreLocks(saved);
    }
}

/* The watched m4a functions (port/test/audio/snd_calls.py WATCHED), wrapped
   (--wrap, port/test/Makefile PC_WRAPS): every call from outside the driver's
   objects reaches these, and one made while none of them is open is the
   driver's top-level call, as the GBA capture's LR rule says (calls inside
   m4a.o, and its calls into m4a_1_pc.o, are made within an open one). Counted
   for the main loop only (no IRQ handler, no OnVBlank). They change nothing
   else in the run. */
static void SndCallEnter(void)
{
    if (sSndDepth++ == 0 && EventsOn() && gPortIrqDepth == 0 && !sInVBlank)
    {
        LagReplaySndDue();
        sSndSince++;
        sSndMainOpen = 1;
        sSndStops = 0;
    }
}

/* The main loop's call is closed before its split VBlank runs: that VBlank's
   own m4a calls (OnVBlank's m4aSoundVSync and m4aSoundMain, wrapped too) then
   find no main-loop call open, as on the GBA, where the handler's calls are
   no main-loop calls. Closed after it (before 2026-10-22), each of them came
   back here at depth 0 with the main loop's call still open and ran the same
   pending VBlank again inside itself, until the 256 slots of the tick were
   full (H-story-1: route_ch6 tick 31,576, route_ch8 20,426). */
static void SndCallExit(void)
{
    if (--sSndDepth == 0 && sSndMainOpen)
    {
        sSndMainOpen = 0;
        LagReplaySplitExit();
    }
}

/* Prototypes as include/m4a.h and include/gba/m4a_internal.h (structs by pointer). */
#define SND_WRAP0(name)                                                                                              \
    void __real_##name(void);                                                                                        \
    void __wrap_##name(void)                                                                                         \
    {                                                                                                                \
        SndCallEnter();                                                                                              \
        __real_##name();                                                                                             \
        SndCallExit();                                                                                               \
    }
#define SND_WRAP1(name, t1)                                                                                          \
    void __real_##name(t1);                                                                                          \
    void __wrap_##name(t1 a)                                                                                         \
    {                                                                                                                \
        SndCallEnter();                                                                                              \
        __real_##name(a);                                                                                            \
        SndCallExit();                                                                                               \
    }
#define SND_WRAP2(name, t1, t2)                                                                                      \
    void __real_##name(t1, t2);                                                                                      \
    void __wrap_##name(t1 a, t2 b)                                                                                   \
    {                                                                                                                \
        SndCallEnter();                                                                                              \
        __real_##name(a, b);                                                                                         \
        SndCallExit();                                                                                               \
    }
#define SND_WRAP3(name, t1, t2, t3)                                                                                  \
    void __real_##name(t1, t2, t3);                                                                                  \
    void __wrap_##name(t1 a, t2 b, t3 c)                                                                             \
    {                                                                                                                \
        SndCallEnter();                                                                                              \
        __real_##name(a, b, c);                                                                                      \
        SndCallExit();                                                                                               \
    }
SND_WRAP0(m4aSoundVSync)
SND_WRAP0(m4aSoundMain)
SND_WRAP0(m4aSoundInit)
SND_WRAP1(m4aSongNumStart, uint16_t)
SND_WRAP1(m4aSongNumStop, uint16_t)
SND_WRAP2(MPlayStart, void *, void *)
SND_WRAP1(m4aSoundMode, uint32_t)
SND_WRAP1(m4aMPlayStop, void *)
SND_WRAP1(m4aMPlayImmInit, void *)
SND_WRAP3(m4aMPlayVolumeControl, void *, uint16_t, uint16_t)
SND_WRAP3(m4aMPlayPanpotControl, void *, uint16_t, int8_t)
SND_WRAP2(m4aMPlayFadeOut, void *, uint16_t)
SND_WRAP2(m4aMPlayFadeOutTemporarily, void *, uint16_t)
SND_WRAP2(m4aMPlayFadeIn, void *, uint16_t)
SND_WRAP1(m4aSongNumStartOrChange, uint16_t)
SND_WRAP1(m4aSongNumStartOrContinue, uint16_t)
SND_WRAP1(m4aSongNumContinue, uint16_t)
SND_WRAP0(m4aMPlayAllStop)
SND_WRAP1(m4aMPlayContinue, void *)
SND_WRAP0(m4aMPlayAllContinue)
SND_WRAP2(m4aMPlayTempoControl, void *, uint16_t)
SND_WRAP3(m4aMPlayPitchControl, void *, uint16_t, int16_t)
SND_WRAP3(m4aMPlayModDepthSet, void *, uint16_t, uint8_t)
SND_WRAP3(m4aMPlayLFOSpeedSet, void *, uint16_t, uint8_t)
SND_WRAP1(MPlayContinue, void *)
SND_WRAP2(MPlayFadeOut, void *, uint16_t)
SND_WRAP3(MPlayOpen, void *, void *, uint8_t)
SND_WRAP1(SoundInit, void *)
SND_WRAP0(SoundClear)
SND_WRAP0(m4aSoundVSyncOff)
SND_WRAP0(m4aSoundVSyncOn)
SND_WRAP1(SampleFreqSet, uint32_t)
SND_WRAP1(MPlayExtender, void *)

/* At the sample: the rest of the tick's lag VBlanks, then its early flush
   VBlank if it has one (the GBA's came after the main loop set the flag), then
   the tick's check. */
static void LagReplayEndOfTick(void)
{
    if (sLagFirst == NULL)
        return;
    while (sLagDone < LagCountInLoop(sTick))
        RunLagVBlank(0);
    if (sLagDone < LagCount(sTick))
        RunLagVBlank(1);
    if (sTick < sLagTicks && sEvents != sGbaEvents[sTick])
    {
        if (sEventMismatch < COUNT(sEventMismatchTicks))
            sEventMismatchTicks[sEventMismatch] = sTick;
        sEventMismatch++;
    }
}

/* Sample: the tick's events and, in the lag replay, where its lag VBlanks ran. */
static void SampleEvents(void)
{
    unsigned i;

    fprintf(sOut, ",\"events\":%u", sEvents);
    if (sLagUsedCount > 0)
    {
        int split = 0, head = 0;

        fprintf(sOut, ",\"lag_at\":[");
        for (i = 0; i < sLagUsedCount; i++)
        {
            fprintf(sOut, i ? ",%u" : "%u", sLagUsed[i] & LAG_USED_EVENTS);
            split |= (sLagUsed[i] & LAG_USED_SPLIT) != 0;
            head |= (sLagUsed[i] & LAG_USED_HEAD) != 0;
        }
        fputc(']', sOut);
        if (head) /* run at a RunProcessScript's head as the GBA's record says (LagReplayHead) */
        {
            const char *sep = "";

            fprintf(sOut, ",\"lag_head\":[");
            for (i = 0; i < sLagUsedCount; i++)
                if (sLagUsed[i] & LAG_USED_HEAD)
                {
                    fprintf(sOut, "%s%u", sep, i);
                    sep = ",";
                }
            fputc(']', sOut);
        }
        if (sLagSndOn) /* sound positions: the m4a calls since the event each ran at */
        {
            const char *sep = "";

            fprintf(sOut, ",\"lag_snd\":[");
            for (i = 0; i < sLagUsedCount; i++)
                fprintf(sOut, i ? ",%u" : "%u", (sLagUsed[i] >> LAG_USED_SND_SHIFT) & LAG_USED_SND_MAX);
            fputc(']', sOut);
            for (i = 0; i < sLagUsedCount; i++) /* and where inside an m4a call (LagReplaySplitExit) */
                if (sLagUsedStops[i] >= 0)
                {
                    fprintf(sOut, "%s[%u,%d,%d]", *sep ? sep : ",\"lag_snd_split\":[", i, sLagUsedStops[i],
                        sLagUsedLocks[i]);
                    sep = ",";
                }
            if (*sep)
                fputc(']', sOut);
            sep = "";
            for (i = 0; i < sLagUsedCount; i++) /* and the locks of one nested in an IRQ handler's call */
                if (sLagUsedNested[i] >= 0)
                {
                    fprintf(sOut, "%s[%u,%d]", *sep ? sep : ",\"lag_snd_nested\":[", i, sLagUsedNested[i]);
                    sep = ",";
                }
            if (*sep)
                fputc(']', sOut);
        }
        if (split) /* the BIOS copies split as the GBA's record says (LagSplitCall) */
        {
            const char *sep = "";

            fprintf(sOut, ",\"lag_split\":[");
            for (i = 0; i < sLagUsedCount; i++)
                if (sLagUsed[i] & LAG_USED_SPLIT)
                {
                    fprintf(sOut, "%s[%u,%d,%d]", sep, i, sLagSplit.written[sLagFirst[sTick] + i],
                        sLagSplit.read[sLagFirst[sTick] + i]);
                    sep = ",";
                }
            fputc(']', sOut);
        }
    }
    if (sEarlyRan)
        fprintf(sOut, ",\"early_flush\":1"); /* the last lag_at entry: the early flush VBlank */
    sEvents = 0;
    sSndSince = 0;
    sLagDone = 0;
    sLagUsedCount = 0;
    sEarlyRan = 0;
}

static void Setup(void)
{
    const char *out = getenv("GATE0_OUT");
    const char *raw = getenv("GATE0_RAW");
    const char *ticks = getenv("GATE0_TICKS");
    const char *cheat = getenv("GATE0_CHEAT");
    unsigned i;

    LoadSramIn(); /* also without GATE0_OUT */
    sState = -1;
    if (cheat != NULL && *cheat != '\0')
    {
        CheatSetup(cheat); /* sCheat: the name in its order */
        if (out == NULL)
            Fail("GATE0_CHEAT is for Gate 0 runs: GATE0_OUT is not set", NULL);
    }
    if (out == NULL)
    {
        const char *in = getenv("GATE0_STATE_IN"), *save = getenv("GATE0_STATE_OUT");

        if ((in != NULL && *in != '\0') || (save != NULL && *save != '\0'))
            Fail("save states (GATE0_STATE_IN, GATE0_STATE_OUT) are for Gate 0 runs: GATE0_OUT is not set", NULL);
        return;
    }
    if (ticks != NULL)
        sTicks = (uint32_t)strtoul(ticks, NULL, 0);
    /* GATE0_STATE_OUT/GATE0_STATE_IN (save states): before the GBA trace readers
       below, which index a resumed trace by absolute tick; on a resume it puts
       the snapshot's SRAM fragment and "resume" block into sSramHeader. */
    PortSnap_Setup(sSramHeader, sizeof(sSramHeader), sTicks);
    if (getenv("GATE0_KEYS") != NULL)
    {
        sKeysPath = getenv("GATE0_KEYS");
        ReadKeys(sKeysPath);
    }
    if (getenv("GATE0_SHOT_TICKS") != NULL)
        ReadShotTicks(getenv("GATE0_SHOT_TICKS"));
    sShotDir = getenv("GATE0_SHOTDIR");
    sOut = fopen(out, "w");
    if (sOut == NULL)
        Fail("cannot open ", out);
    if (raw != NULL && (sRaw = fopen(raw, "wb")) == NULL)
        Fail("cannot open ", raw);
    if (getenv("GATE0_SND_INDEX") != NULL && *getenv("GATE0_SND_INDEX") != '\0')
    {
        sSndIndex = fopen(getenv("GATE0_SND_INDEX"), "w");
        if (sSndIndex == NULL)
            Fail("cannot open ", getenv("GATE0_SND_INDEX"));
        fprintf(sSndIndex, "# fe8-sndindex-1 TICK V: V the sound record of the VBlank the tick waits for (pc)\n");
    }
    if (getenv("GATE0_LAG_FROM") != NULL)
    {
        if (getenv("GATE0_CLOCK_FROM") != NULL || getenv("GATE0_AFFINE_FROM") != NULL)
            Fail("GATE0_LAG_FROM runs on its own (no GATE0_CLOCK_FROM or GATE0_AFFINE_FROM)", NULL);
        ReadGbaLag(getenv("GATE0_LAG_FROM"));
        /* A lag replay saved at tick T (GATE0_STATE_AT): the photo is taken at
           T's sample point, after T's main loop, whose lag VBlanks must have
           run at their GBA positions by then, so the GBA trace needs tick T. */
        if (PortSnap_LastSaveTick() != 0 && PortSnap_LastSaveTick() >= sLagTicks)
        {
            char msg[256];

            snprintf(msg, sizeof(msg), "a lag replay snapshot at tick %u needs the GBA's tick %u (its lag VBlanks "
                                       "inside the main loop come before the photo), and the GBA trace ends at "
                                       "tick %u: ", PortSnap_LastSaveTick(), PortSnap_LastSaveTick(), sLagTicks - 1);
            Fail(msg, getenv("GATE0_LAG_FROM"));
        }
    }
    if (getenv("GATE0_CLOCK_FROM") != NULL)
        ReadGbaClock(getenv("GATE0_CLOCK_FROM"));
    if (getenv("GATE0_AFFINE_FROM") != NULL)
        OpenGbaAffine(getenv("GATE0_AFFINE_FROM"));
    if (getenv("GATE0_RASTER_FROM") != NULL && *getenv("GATE0_RASTER_FROM") != '\0')
    {
        /* The affine replay's frame of record is its own replayed frame, and the
           clock replay changes the run: the raster-timing redraw goes with the
           plain run and the lag replay only. */
        if (getenv("GATE0_CLOCK_FROM") != NULL || getenv("GATE0_AFFINE_FROM") != NULL)
            Fail("GATE0_RASTER_FROM goes with the plain run or the lag replay (no GATE0_CLOCK_FROM or "
                 "GATE0_AFFINE_FROM)", NULL);
        OpenGbaRaster(getenv("GATE0_RASTER_FROM"));
    }
    EventsOn(); /* maps this binary once: the proc.c addresses and sExeCrc */
    fprintf(sOut, "{\"source\":\"pc\",\"format\":\"fe8-gate0-trace-4\",\"exe_crc32\":\"%08x\",\"keys\":\"%s\","
                  "\"ticks\":%u%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s}\n",
        sExeCrc, sKeysPath, sTicks, sGbaClock ? ",\"clock_from\":\"" : "",
        sGbaClock ? getenv("GATE0_CLOCK_FROM") : "", sGbaClock ? "\"" : "",
        sAffineFrom ? ",\"affine_from\":\"" : "", sAffineFrom ? sAffinePath : "", sAffineFrom ? "\"" : "",
        sLagPath ? ",\"lag_from\":\"" : "", sLagPath ? sLagPath : "", sLagPath ? "\"" : "",
        sRaster.path ? ",\"raster_from\":\"" : "", sRaster.path ? sRaster.path : "", sRaster.path ? "\"" : "",
        sSramHeader, sCheat ? ",\"cheat\":\"" : "", sCheat ? sCheat : "", sCheat ? "\"" : "");

    sRaws[3].host = gKeyStatusPtr;
    if ((uint32_t)(uintptr_t)GBA_gRNSeeds != sRaws[2].gbaAddr || GBA_gLCGRNValue != sScalars[5].host)
        Fail("field table out of order", NULL);

    /* Report fields the host keeps away from their GBA address. */
    for (i = 0; i < COUNT(sScalars) + COUNT(sRaws) + COUNT(sHashed); i++)
    {
        const struct Field *f = i < COUNT(sScalars) ? &sScalars[i]
            : i < COUNT(sScalars) + COUNT(sRaws) ? &sRaws[i - COUNT(sScalars)]
            : &sHashed[i - COUNT(sScalars) - COUNT(sRaws)];
        if ((uint32_t)(uintptr_t)f->host != f->gbaAddr)
            PortLog("[gate0] %s is at host 0x%08X, GBA 0x%08X\n", f->name,
                (uint32_t)(uintptr_t)f->host, f->gbaAddr);
    }
    PortLog("[gate0] tracing %u ticks to %s (keys: %s, %u entries)\n", sTicks, out,
        sKeysPath[0] ? sKeysPath : "none", sMaskCount);
    if (sCheat != NULL)
        PortLog("[gate0] test cheat GATE0_CHEAT=%s (README.md, \"Test cheats\"): at each tick's sample%s\n", sCheat,
            (sCheatOn & CHEAT_CONVOY) ? "; the convoy's list: 177 of gItemData's 205 items (the rest are named "
                                        "\"Dummy\")" : "");
    if ((sCheatOn & CHEAT_CONVOY) && sCheatItemCount != 177)
        Fail("GATE0_CHEAT=convoy: the item list is not the 177 items test_cheat.lua names", NULL);
    sState = 1;
}

static void Sample(void)
{
    uint32_t frame = gPortFrameCount + 1; /* the VBlank this tick waits for */
    unsigned i, j;

    fprintf(sOut, "{\"tick\":%u,\"frame\":%u,\"vblanks\":%u,\"keys\":\"%03x\"", sTick, frame,
        frame - sLastTickFrame, MaskFor(sTick));
    for (i = 0; i < COUNT(sScalars); i++)
    {
        const struct Field *f = &sScalars[i];
        uint32_t v = 0;
        memcpy(&v, f->host, f->size);
        fprintf(sOut, ",\"%s\":\"%0*x\"", f->name, (int)(f->size * 2), v);
    }
    for (i = 0; i < COUNT(sRaws); i++)
    {
        fprintf(sOut, ",\"%s\":\"", sRaws[i].name);
        for (j = 0; j < sRaws[i].size; j++)
            fprintf(sOut, "%02x", sRaws[i].host[j]);
        fputc('"', sOut);
    }
    for (i = 0; i < COUNT(sHashed); i++)
    {
        fprintf(sOut, ",\"%s\":\"%08x\"", sHashed[i].name, Fnv1a(sHashed[i].host, sHashed[i].size));
        if (sRaw != NULL)
            fwrite(sHashed[i].host, 1, sHashed[i].size, sRaw);
    }
    for (i = 0; i < COUNT(sHashedOnly); i++)
        fprintf(sOut, ",\"%s\":\"%08x\"", sHashedOnly[i].name, Fnv1a(sHashedOnly[i].host, sHashedOnly[i].size));
    /* The counters as they are at the sample (the frame step changes onvblank_calls). */
    fprintf(sOut, ",\"onmain_calls\":%u,\"vblank_waits\":%u,\"onvblank_calls\":%u", gPortOnMainCalls,
        sVBlankWaits, sOnVBlankCalls);
    SampleEvents();
    /* frame15 follows in FinishSample, after the frame step. */
    sLastTickFrame = frame;
}

/* Linked with --wrap=OnVBlank: every SetInterrupt_LCDVBlank(OnVBlank) is in
   another object than bm.o, so the IRQ table holds this wrapper. */
void __wrap_OnVBlank(void)
{
    sOnVBlankCalls++;
    if (sGbaClock != NULL && sTick >= 1 && sTick - 1 < sGbaClockCount)
        memcpy(gGameClock, &sGbaClock[sTick - 1], 4); /* clock replay, see ReadGbaClock */
    sInVBlank++; /* its events are not main-loop events */
    __real_OnVBlank();
    sInVBlank--;
}

void __wrap_UpdateKeyStatus(void *keyStatus)
{
    if (sState == 0)
        Setup();
    if (sState > 0) /* active low, as the GBA register */
        *(volatile uint16_t *)(uintptr_t)REG_KEYINPUT_ADDR = (uint16_t)(0x3FF & ~MaskFor(sTick));
    __real_UpdateKeyStatus(keyStatus);
}

void __wrap_VBlankIntrWait(void)
{
    int sampled = 0;
    uint32_t tick = 0;

    sVBlankWaits++; /* counted before the sample, as the GBA breakpoint is */
    if (sState == 0)
        Setup();
    if (sState > 0 && gBmSt[0] != 0) /* gBmSt.main_loop_ended: an OnMain just finished */
    {
        /* Save states: the snapshot point. A resume replaces the whole state
           here (this hook's counters too) and goes on as the snapshot's tick. */
        uint32_t resumed = PortSnap_AtSample(sTick, sOut, sRaw);

        if (resumed != 0xFFFFFFFFu && resumed != sTick)
            Fail("the save state did not restore the hook's tick", NULL);
        if (resumed != 0xFFFFFFFFu)
            CheatResume(); /* the photo's cheats must be this run's; the convoy cursor */
        LagReplayEndOfTick(); /* lag replay, see ReadGbaLag */
        Sample();
        if (sCheat != NULL)
            /* The test cheats at this tick boundary: after the sample, before
               the VBlank the tick waits for (the frame step below), where the
               GBA capture runs them (README.md, "Test cheats"). */
            CheatRefill();
        if (sSndIndex != NULL) /* every lag VBlank of the tick has run: the frame step's record is next */
            fprintf(sSndIndex, "%u %u\n", sTick, PortAudio_NextRecord());
        tick = sTick++;
        sampled = 1;
        if (sAffineFrom != NULL)
            AffinePrepare(tick); /* affine replay, see OpenGbaAffine */
        RasterPrepare(tick);     /* raster-timing frames, see OpenGbaRaster */
    }
    __real_VBlankIntrWait();
    if (sampled)
    {
        FinishSample(tick); /* the frame drawn while this tick waited */
        if (sTick >= sTicks)
        {
            const char *sram = getenv("GATE0_SRAM");

            fclose(sOut);
            if (sRaw != NULL)
                fclose(sRaw);
            if (sSndIndex != NULL)
                fclose(sSndIndex);
            if (sram != NULL)
            {
                FILE *f = fopen(sram, "wb");

                if (f == NULL)
                    Fail("cannot open ", sram);
                fwrite((const void *)(uintptr_t)0x0E000000u, 1, 0x10000, f); /* D1-a: SRAM at its GBA address */
                fclose(f);
            }
            PortLog("[gate0] %u ticks in %u frames, %u OnMain calls\n", sTick, gPortFrameCount,
                gPortOnMainCalls);
            if (sCheatOn & CHEAT_DURABILITY)
                PortLog("[gate0] test cheat GATE0_CHEAT=%s: %u item words refilled, on %u ticks\n", sCheat,
                    sCheatWords, sCheatTicks);
            if (sCheatOn & CHEAT_FUNDS)
                PortLog("[gate0] test cheat GATE0_CHEAT=%s: the gold set to 999,999 on %u ticks\n", sCheat,
                    sCheatFundsTicks);
            if (sCheatOn & CHEAT_CONVOY)
                PortLog("[gate0] test cheat GATE0_CHEAT=%s: %u items put in the convoy (cursor %u, %u items in the "
                        "list)\n", sCheat, sCheatPlaced, sCheatConvoy, sCheatItemCount);
            if (sAffineFrom != NULL)
                PortLog("[gate0] affine replay: frames drawn with the GBA's bg2affin %u, equal %u, "
                        "registers not the PC's shadow %u, written mid-frame %u, no GBA tick %u\n",
                    sAffineCounts[0], sAffineCounts[1], sAffineCounts[2], sAffineCounts[3], sAffineCounts[4]);
            if (sRaster.path != NULL)
            {
                unsigned i;

                PortLog("[gate0] raster replay: frame15 frames by mark:");
                for (i = 0; i < RRM_COUNT; i++)
                    PortLog(" %s %u", kRrMarks[i], sRaster.counts[i]);
                PortLog("\n");
            }
            if (sLagPath != NULL)
            {
                unsigned i;

                PortLog("[gate0] lag replay: %u GBA lag VBlanks run (%u of them early flush, %u inside a BIOS copy, "
                        "%u at a script's head), %u at the GBA's position, %u later (the PC tick had fewer events); "
                        "ticks whose event count is not the GBA's: %u",
                    sLagRuns, sEarlyRuns, sLagSplit.runs, sLagHead.runs, sLagExact, sLagRuns - sLagExact,
                    sEventMismatch);
                for (i = 0; i < sEventMismatch && i < COUNT(sEventMismatchTicks); i++)
                    PortLog("%s%u", i ? ", " : " (", sEventMismatchTicks[i]);
                PortLog("%s\n", sEventMismatch == 0 ? "" : sEventMismatch > COUNT(sEventMismatchTicks) ? ", ...)" : ")");
            }
            exit(0);
        }
    }
}
