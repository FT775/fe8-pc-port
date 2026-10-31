/*
 * Headless mGBA Lua runner for the Gate 0 oracle (port-plan/PLAN.md, Gate 0).
 *
 * The Ubuntu mgba-qt and mgba-sdl 0.10.5 frontends have no command-line
 * option to load a Lua script and no headless mode. The distribution library
 * (libmgba0.10t64, built with ENABLE_SCRIPTING and USE_LUA) has both, so this
 * small program drives it directly: no window, no audio device, no user
 * config, no BIOS file (mGBA's built-in HLE BIOS).
 *
 *   mgba_lua_run ROM SCRIPT.lua MAX_FRAMES
 *
 * It loads ROM, loads SCRIPT into a Lua scripting context with the core
 * attached as `emu`, then runs at most MAX_FRAMES frames. After each frame it
 * fires the scripting "frame" callback; the frame has just ended (the VBlank
 * line was reached), before the CPU has taken the VBlank IRQ. The script ends
 * the run early with os.exit().
 *
 * Breakpoints (optional): MGBA_RUN_BP="name@0xADDR[,name@0xADDR...]" sets a
 * libmgba debugger (hardware) breakpoint at each ADDR. Each time the CPU is
 * about to execute the instruction at ADDR, the runner fires the scripting
 * callback `name` (register it with callbacks:add("name", fn)); the script can
 * read memory there, before that instruction runs. With breakpoints set, the
 * runner steps the core one instruction at a time, as mDebuggerRun does,
 * instead of runFrame: the emulated machine is the same, only slower. The
 * runner looks up the next instruction's address in its own table and calls
 * the library's breakpoint check only at a breakpoint's address (RunnerStep),
 * so a breakpoint costs its hits, not every instruction.
 *
 * Returns (optional): "name@0xADDR:ret" also reports the return of each call
 * that reached ADDR, as the callback `name_ret`. When the breakpoint fires the
 * runner keeps the caller's return address (LR) and stack pointer; the call
 * has returned when the next instruction is at that address with that stack
 * pointer (an IRQ handler reaching the same code runs lower on the stack).
 * Calls return in the reverse order of their entries, also across IRQs, so one
 * stack of open calls is enough.
 *
 * Raster log (optional): MGBA_RUN_RASTERLOG=FILE, see RasterInstall below and
 * port/test/README.md, "Raster-timing frames".
 *
 * Built only against the distribution's libmgba-dev headers (port/test/Makefile).
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mgba/core/core.h>
#include <mgba/core/config.h>
#include <mgba/core/log.h>
#include <mgba/core/scripting.h>
#include <mgba/core/serialize.h>
#include <mgba/core/timing.h>
#include <mgba/debugger/debugger.h>
#include <mgba/internal/arm/arm.h>
#include <mgba/internal/arm/debugger/debugger.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/script/context.h>
#include <mgba/script/macros.h>
#include <mgba-util/vfs.h>

#include <fcntl.h>

/* Defined by libmgba (src/script/engines/lua.c); no installed header declares it. */
extern struct mScriptEngine2 *const mSCRIPT_ENGINE_LUA;

static void LogToStderr(struct mLogger *logger, int category, enum mLogLevel level,
                        const char *format, va_list args)
{
    (void)logger;
    /* Script console output and real problems only; the GBA core is chatty
       about stubbed BIOS calls and unaligned accesses at debug levels. */
    if (strcmp(mLogCategoryId(category), "script") != 0 &&
        !(level & (mLOG_FATAL | mLOG_ERROR | mLOG_WARN)))
        return;
    fprintf(stderr, "[mgba %s] ", mLogCategoryName(category));
    vfprintf(stderr, format, args);
    fputc('\n', stderr);
}

/*
 * Start from a save (optional; port/test/README.md, "Chapter saves"):
 * GATE0_SRAM_IN=PATH gives the core the first 32 KiB of PATH as its save data
 * before reset, as a frontend loads a .sav. 32 KiB is FE8's SRAM (D-18); a
 * 64 KiB GATE0_SRAM dump holds mGBA's mirror of it in the upper half, which is
 * not used. The data goes into a memory VFile, so nothing is ever written
 * back to PATH. The run prints the FNV-1a 32 of those 32 KiB; the trace
 * header carries the same value ("sram_fnv"), and the PC hook loads the same
 * bytes (port/test/pc/gate0_hook.c), so both sides boot with equal SRAM.
 */
#define SRAM_IN_SIZE 0x8000

static int LoadSramIn(struct mCore *core)
{
    static uint8_t buf[SRAM_IN_SIZE];
    const char *path = getenv("GATE0_SRAM_IN");
    struct VFile *vf, *mem;
    ssize_t size, got;
    uint32_t hash = 0x811C9DC5u;
    size_t i;

    if (path == NULL || *path == '\0')
        return 1;
    vf = VFileOpen(path, O_RDONLY);
    if (vf == NULL)
    {
        fprintf(stderr, "mgba_lua_run: cannot open GATE0_SRAM_IN %s\n", path);
        return 0;
    }
    size = vf->size(vf);
    got = (size == SRAM_IN_SIZE || size == 2 * SRAM_IN_SIZE) ? vf->read(vf, buf, sizeof(buf)) : -1;
    vf->close(vf);
    if (got != (ssize_t)sizeof(buf))
    {
        fprintf(stderr, "mgba_lua_run: GATE0_SRAM_IN %s: want a 32 KiB or 64 KiB save, got %ld bytes\n",
            path, (long)size);
        return 0;
    }
    mem = VFileMemChunk(buf, sizeof(buf));
    if (mem == NULL || !core->loadSave(core, mem))
    {
        fprintf(stderr, "mgba_lua_run: the core did not take GATE0_SRAM_IN %s\n", path);
        return 0;
    }
    for (i = 0; i < sizeof(buf); i++)
        hash = (hash ^ buf[i]) * 0x01000193u;
    fprintf(stderr, "mgba_lua_run: SRAM from %s (32768 bytes, fnv1a %08x)\n", path, (unsigned)hash);
    return 1;
}

static struct mLogger sLogger = { .log = LogToStderr, .filter = NULL };

#define MAX_BREAKPOINTS 64
#define MAX_OPEN_CALLS 64
#define MAX_IRQ_BEFORE 16

static struct
{
    char name[48];
    char irqName[52], resumeName[56]; /* name + "_irq", name + "_resume" (see the run loop) */
    char retName[52];   /* name + "_ret" when returns are reported, else "" */
    uint32_t address;
    /* IRQs taken at this address before its instruction ran, innermost last:
       the interrupted code's stack pointer and CPU mode, which tell its
       repeated report apart from a call that the IRQ handler makes to the
       same address (see the run loop). */
    struct
    {
        uint32_t sp;
        int mode;
    } irqBefore[MAX_IRQ_BEFORE];
    unsigned irqBeforeCount;
    unsigned long hits, rehits, returns;
} sBreak[MAX_BREAKPOINTS];
static int sBreakCount;
static int sBreakHit = -1;

/* Open calls of ":ret" breakpoints, innermost last. */
static struct
{
    int bp;
    uint32_t ret, sp;
} sOpen[MAX_OPEN_CALLS];
static int sOpenCount;

/* MGBA_RUN_BP="name@0xADDR[:ret],..." (see the header comment). */
static int ParseBreakpoints(const char *spec)
{
    char buf[4096], *item, *save = NULL;

    if (spec == NULL || *spec == '\0')
        return 1;
    if (strlen(spec) >= sizeof(buf))
        return 0;
    strcpy(buf, spec);
    for (item = strtok_r(buf, ",", &save); item != NULL; item = strtok_r(NULL, ",", &save))
    {
        char *at = strchr(item, '@'), *end;
        if (at == NULL || at == item || (size_t)(at - item) >= sizeof(sBreak[0].name) ||
            sBreakCount == MAX_BREAKPOINTS)
            return 0;
        memcpy(sBreak[sBreakCount].name, item, (size_t)(at - item));
        sBreak[sBreakCount].name[at - item] = '\0';
        snprintf(sBreak[sBreakCount].irqName, sizeof(sBreak[0].irqName), "%s_irq", sBreak[sBreakCount].name);
        snprintf(sBreak[sBreakCount].resumeName, sizeof(sBreak[0].resumeName), "%s_resume",
            sBreak[sBreakCount].name);
        sBreak[sBreakCount].address = (uint32_t)strtoul(at + 1, &end, 0);
        if (strcmp(end, ":ret") == 0)
        {
            memcpy(sBreak[sBreakCount].retName, item, (size_t)(at - item));
            memcpy(sBreak[sBreakCount].retName + (at - item), "_ret", 5);
        }
        else if (*end != '\0')
            return 0;
        sBreakCount++;
    }
    return 1;
}

/* At exit (also from the script's os.exit()): calls passed to the script and
   repeated reports after an IRQ (see the run loop). */
static void PrintBreakStats(void)
{
    int i;

    for (i = 0; i < sBreakCount; i++)
        if (sBreak[i].retName[0] != '\0')
            fprintf(stderr, "mgba_lua_run: breakpoint %s: %lu calls, %lu returns, %lu repeated reports after an IRQ skipped\n",
                sBreak[i].name, sBreak[i].hits, sBreak[i].returns, sBreak[i].rehits);
        else
            fprintf(stderr, "mgba_lua_run: breakpoint %s: %lu calls, %lu repeated reports after an IRQ skipped\n",
                sBreak[i].name, sBreak[i].hits, sBreak[i].rehits);
}

/* Called from mDebuggerEnter when a breakpoint is reached. The callback is
   fired from the run loop, after RunnerStep returns, so the script runs
   outside the debugger. The CPU is stopped before the instruction at the
   breakpoint; the next RunnerStep resumes (no `paused` handler). */
static void OnDebuggerEntered(struct mDebugger *debugger, enum mDebuggerEntryReason reason,
                              struct mDebuggerEntryInfo *info)
{
    int i;

    (void)debugger;
    if (reason != DEBUGGER_ENTER_BREAKPOINT || info == NULL)
        return;
    for (i = 0; i < sBreakCount; i++)
        if (sBreak[i].address == info->address)
            sBreakHit = i;
}

/*
 * One step of the run loop: what mDebuggerRun does in libmgba 0.10.5 with
 * breakpoints set (port/test/README.md, "Headless Lua"), minus the calls that
 * cannot find anything. In state RUNNING mDebuggerRun calls core->step and
 * then the ARM platform's checkBreakpoints, which looks for a breakpoint at
 * the next instruction's address (PC less 4 in ARM state, 2 in Thumb) and, at
 * one, enters the debugger (mDebuggerEnter: state PAUSED, cpu->nextEvent =
 * cpu->cycles, then OnDebuggerEntered). In state PAUSED, with no `paused`
 * handler, it only sets RUNNING again, so the step after a hit runs no
 * instruction. That check walks the whole breakpoint list on every
 * instruction, with two library calls per breakpoint: with gba-trace's 13
 * it was about 44% of the capture's time (perf, 15,000 ticks of the scripted
 * route; review rv-h8). Here core->step is the same call, and
 * checkBreakpoints, the library's own, is called only when the next address
 * is in sBpTable, the breakpoint addresses. Anywhere else it would do
 * nothing: no breakpoint has a condition, and there are no watchpoints and no
 * stack trace (RunnerStepCheck refuses to run otherwise). Every other state
 * goes to mDebuggerRun itself.
 */
#define BP_TABLE_SIZE 256 /* a power of two, above 2 * MAX_BREAKPOINTS */
static uint32_t sBpTable[BP_TABLE_SIZE]; /* open addressing; 0 is a free slot (no instruction runs at 0 here) */

static unsigned BpSlot(uint32_t address)
{
    return (unsigned)((address >> 1) * 0x9E3779B1u >> 24) & (BP_TABLE_SIZE - 1);
}

static int BpTableHas(uint32_t address)
{
    unsigned i;

    for (i = BpSlot(address); sBpTable[i] != 0; i = (i + 1) & (BP_TABLE_SIZE - 1))
        if (sBpTable[i] == address)
            return 1;
    return 0;
}

static int RunnerStepCheck(struct mDebugger *debugger)
{
    struct ARMDebugger *arm = (struct ARMDebugger *)debugger->platform;
    int b;

    memset(sBpTable, 0, sizeof(sBpTable));
    for (b = 0; b < sBreakCount; b++)
    {
        unsigned i;

        if (sBreak[b].address == 0)
        {
            fprintf(stderr, "mgba_lua_run: a breakpoint at address 0 is not supported\n");
            return 0;
        }
        for (i = BpSlot(sBreak[b].address); sBpTable[i] != 0 && sBpTable[i] != sBreak[b].address;
             i = (i + 1) & (BP_TABLE_SIZE - 1))
            ;
        sBpTable[i] = sBreak[b].address;
    }
    if (arm->stackTraceMode != 0 || mWatchpointListSize(&arm->watchpoints) != 0)
    {
        fprintf(stderr, "mgba_lua_run: the debugger has a stack trace or watchpoints; RunnerStep does not check them\n");
        return 0;
    }
    return 1;
}

static void RunnerStep(struct mDebugger *debugger, struct mCore *core, struct ARMCore *cpu)
{
    if (debugger->state == DEBUGGER_RUNNING)
    {
        core->step(core);
        if (BpTableHas((uint32_t)cpu->gprs[ARM_PC] - (cpu->cpsr.t ? WORD_SIZE_THUMB : WORD_SIZE_ARM)))
            debugger->platform->checkBreakpoints(debugger->platform);
    }
    else
        mDebuggerRun(debugger);
}

/*
 * Save states (port/test/README.md, "Save states"; test harness only).
 * MGBA_RUN_STATE_IN=FILE loads an mGBA state (with its SRAM) right after
 * reset, before the first instruction; the script sees the path as the global
 * runner_state_loaded ("" without one). The script saves one with
 * runner_save_state(FILE) (1 done, 0 refused or failed): it is refused while
 * the runner has an IRQ-at-breakpoint report pending or a ":ret" call open,
 * since that runner state is not in the file. runner_rom_crc32 is the CRC-32
 * of the ROM file, as the PC hook takes it of the ROM it loaded.
 */
static struct mCore *sCore;

static int32_t RunnerSaveState(const char *path)
{
    struct VFile *vf;
    int b, ok;

    for (b = 0; b < sBreakCount; b++)
        if (sBreak[b].irqBeforeCount != 0)
        {
            fprintf(stderr, "mgba_lua_run: save state refused: an IRQ came at breakpoint %s and its repeated "
                            "report is pending\n", sBreak[b].name);
            return 0;
        }
    if (sOpenCount != 0)
    {
        fprintf(stderr, "mgba_lua_run: save state refused: %d \":ret\" calls are open\n", sOpenCount);
        return 0;
    }
    vf = VFileOpen(path, O_CREAT | O_TRUNC | O_RDWR); /* the core maps the file to write it */
    if (vf == NULL)
    {
        fprintf(stderr, "mgba_lua_run: cannot write the save state %s\n", path);
        return 0;
    }
    ok = mCoreSaveStateNamed(sCore, vf, SAVESTATE_SAVEDATA);
    vf->close(vf);
    if (!ok)
        fprintf(stderr, "mgba_lua_run: the core did not save its state to %s\n", path);
    return ok ? 1 : 0;
}

mSCRIPT_BIND_FUNCTION(sBindSaveState, S32, RunnerSaveState, 1, CHARP, path);

/*
 * runner_cycles(): the emulated CPU cycles since reset (mTimingGlobalTime), as
 * a float (exact below 2^53, about 17 years of GBA time). At a breakpoint it
 * is the time just before that instruction runs. The TAS route's console
 * timing uses it (port/test/README.md, "TAS route"): a key change listed at a
 * console cycle time is applied at the key read that comes after it. Not kept
 * in a save state.
 */
static double RunnerCycles(void)
{
    return (double)mTimingGlobalTime(sCore->timing);
}

mSCRIPT_BIND_FUNCTION(sBindCycles, F64, RunnerCycles, 0);

/*
 * runner_irq_sp(): the IRQ mode's stack pointer (banked while the CPU is in
 * another mode). At a breakpoint in an IRQ handler (FE8's IrqMain calls it in
 * System mode) the words there are IrqMain's push {spsr, IE, r3, lr} and,
 * above them, the BIOS IRQ vector's push {r0-r3, r12, lr}: the interrupted
 * code's registers. gate0_capture.lua reads them for the lag VBlanks that come
 * inside a BIOS copy (README.md, "Lag position", kind 6).
 */
static uint32_t RunnerIrqSp(void)
{
    struct ARMCore *cpu = sCore->cpu;

    if (cpu->privilegeMode == MODE_IRQ)
        return (uint32_t)cpu->gprs[ARM_SP];
    return (uint32_t)cpu->bankedRegisters[BANK_IRQ][0];
}

mSCRIPT_BIND_FUNCTION(sBindIrqSp, U32, RunnerIrqSp, 0);

/*
 * runner_irq_reg(sp, k): r<k> (k 4 to 14) of the code an IRQ interrupted, as
 * it was when the CPU entered IRQ mode with the IRQ stack pointer at sp (the
 * value before the BIOS vector's push: __sp_irq, 0x03007FA0, for an IRQ that
 * interrupted the main loop), or -1 when no IRQ entered at sp is open. The
 * BIOS vector and FE8's IrqMain save only r0-r3, r12 and lr (the return
 * address) on the IRQ stack; r4-r11 stay in the registers, so they are the
 * interrupted code's only until a handler changes them, and its own r13/r14
 * (sp and lr of its mode) are banked. IrqMain runs the handler in System mode
 * with IRQs on, so an IRQ can come inside a handler (the VBlank inside line
 * 159's HBlank_BgVerticalScroll, 2026-10-21, H-col-2), and at that inner
 * handler r4-r14 are the outer handler's. The run loop keeps them at each IRQ
 * entry (the step that switches the CPU into IRQ mode, which leaves r4-r11 as
 * they were and banks the old mode's r13/r14), in IrqEntryRecord below; an
 * entry is dropped when a later IRQ enters at or above its stack pointer (that
 * IRQ has returned). gate0_capture.lua reads them for a lag VBlank inside a
 * BIOS copy and at a proc script's head (README.md, "Lag position", kinds 6
 * and 3). Not kept in a save state: a snapshot is saved at a "frame" callback
 * while the main loop waits in VBlankIntrWait, so no IRQ open then can have
 * interrupted the main loop's work, and a lookup with no entry returns -1.
 */
#define MAX_IRQ_ENTRIES 16

static struct
{
    uint32_t sp;
    uint32_t r[11]; /* r4-r14, r13 and r14 those of the interrupted mode */
} sIrqEntry[MAX_IRQ_ENTRIES];
static int sIrqEntryCount;

static void IrqEntryRecord(struct ARMCore *cpu, int modeBefore)
{
    uint32_t sp = (uint32_t)cpu->gprs[ARM_SP];
    int i, bank;

    switch (modeBefore)
    {
    case MODE_FIQ: bank = BANK_FIQ; break;
    case MODE_SUPERVISOR: bank = BANK_SUPERVISOR; break;
    case MODE_ABORT: bank = BANK_ABORT; break;
    case MODE_UNDEFINED: bank = BANK_UNDEFINED; break;
    default: bank = BANK_NONE; break; /* User and System */
    }
    while (sIrqEntryCount > 0 && sIrqEntry[sIrqEntryCount - 1].sp <= sp)
        sIrqEntryCount--;
    if (sIrqEntryCount == MAX_IRQ_ENTRIES)
    {
        fprintf(stderr, "mgba_lua_run: more than %d nested IRQs\n", MAX_IRQ_ENTRIES);
        exit(1);
    }
    sIrqEntry[sIrqEntryCount].sp = sp;
    for (i = 0; i < 9; i++) /* r4-r12 (not banked outside FIQ mode) */
        sIrqEntry[sIrqEntryCount].r[i] = (uint32_t)cpu->gprs[4 + i];
    sIrqEntry[sIrqEntryCount].r[9] = (uint32_t)cpu->bankedRegisters[bank][0];  /* r13 */
    sIrqEntry[sIrqEntryCount].r[10] = (uint32_t)cpu->bankedRegisters[bank][1]; /* r14 */
    sIrqEntryCount++;
}

static int64_t RunnerIrqReg(uint32_t sp, int32_t reg)
{
    int i;

    if (reg < 4 || reg > 14)
        return -1;
    for (i = sIrqEntryCount - 1; i >= 0; i--)
        if (sIrqEntry[i].sp == sp)
            return sIrqEntry[i].r[reg - 4];
    return -1;
}

mSCRIPT_BIND_FUNCTION(sBindIrqReg, S64, RunnerIrqReg, 2, U32, sp, S32, reg);

/*
 * Raster log (optional; port/test/README.md, "Raster-timing frames"; decision
 * 9, DIVERGENCES P-17): MGBA_RUN_RASTERLOG=FILE.
 *
 * The GBA draws a frame while its CPU runs: the PC runs OnMain in zero time
 * and then steps the frame (D-15), so the lines of a GBA frame can show the
 * main loop's memory at other moments than the PC's do. For each frame (from
 * one "frame" callback, the start of line 160, to the next) this keeps what is
 * needed to draw that frame on the PC with the GBA's timing:
 *   - the line events, in the order the PC's frame step has them
 *     (port/hw/frame.c, Line): an HBlank or VCount handler entered (IrqMain's
 *     `bx r0` with r2 4 or 8, at the two MGBA_RUN_RASTER_IRQ addresses: its
 *     ROM copy, used before boot copies it, and the IWRAM copy), and a line
 *     drawn (the renderer's drawScanline, which mGBA calls at the line's HBlank).
 *     Events on line 160 are left out: the PC's frame step after a tick's
 *     sample starts at line 161;
 *   - every store that changes a watched halfword, with the number of line
 *     events before it ("pos"). Watched: palette RAM, VRAM and OAM (what the
 *     renderer reads), from the frame's first line event or store with VCOUNT
 *     0-159 on (earlier stores show on every line of the frame); and the ranges
 *     of MGBA_RUN_RASTER_RAM="name@0xADDR+0xSIZE,..." (what the line handlers
 *     read, in EWRAM or IWRAM, and I/O registers below REG_IME, 0x04000208,
 *     such as REG_IE: which line handlers run), every store of the frame. CPU
 *     stores, STM, DMA and the HLE BIOS all store through cpu->memory, which is
 *     wrapped here (an I/O register is read back from mGBA's register image,
 *     gba->memory.io, after the write; REG_DISPSTAT without its status flags,
 *     bits 0-2, which the video unit changes on every line: RasterKeep);
 *   - the FNV-1a 32 of each watched region at the end (line 160).
 * The log checks itself: at the end of each frame each region must equal its
 * copy from where its logging started with the logged stores applied, in
 * order, each one's old value matching; a frame where it does not (a store
 * path that bypasses cpu->memory) or with more line events than kept is
 * written with flag bit 0 ("incomplete"), and the PC labels nothing from it.
 *
 * The script calls runner_raster_write(tick) in a "frame" callback to append
 * the frame that just ended to FILE (it returns the block's CRC-32, as a
 * number, for the trace line; -1 without the log). The log is emptied after
 * every "frame" callback either way.
 *
 * FILE: "RRF1", ROM CRC-32; then blocks, little endian: "RRB1", tick, flags,
 * region count, per region 12-byte name, address, size, FNV-1a 32 at the
 * end; event count, events (u16: kind << 8 | line; kind 1 VCount handler,
 * 2 line drawn, 3 HBlank handler; padded to 4 bytes); store count, stores
 * (16 bytes: u32 address, u16 old, u16 new, u16 pos, u16 flags (bit 0: made
 * inside a line handler), u32 instruction address).
 */
/* Full since fix round 13: 6 RAM and I/O ranges (round 10's 5 and the map
   animations' scanline buffer pointers, P-42) plus the RASTER_VIDEO_REGIONS 3.
   The next watched region needs this and gate0_hook.c's RR_MAX_REGIONS raised
   together. */
#define RASTER_MAX_REGIONS 9
#define RASTER_MAX_EVENTS  2048
#define RASTER_VIDEO_REGIONS 3

struct RasterRegion
{
    char name[12];
    uint32_t addr, size;
    int video;
    uint8_t *copy;       /* the region where its logging started this frame */
    int copied;
};

struct RasterStore
{
    uint32_t addr;
    uint16_t oldValue, newValue, pos, flags;
    uint32_t pc;
};

static FILE *sRasterLog;
static struct RasterRegion sRr[RASTER_MAX_REGIONS];
static unsigned sRrCount;
static struct RasterStore *sRs;
static size_t sRsCount, sRsCap;
static uint16_t sRev[RASTER_MAX_EVENTS];
static unsigned sRevCount, sRevLost;
static int sRasterVideoOn;            /* this frame has reached VCOUNT 0-159: video stores are logged */
static uint32_t sRasterIrqCall[2], sRasterPostIrq[2];
static int sRasterIrqStack[32], sRasterIrqDepth, sRasterLineHandlers;
static unsigned long sRasterIncomplete, sRasterBlocks;
static void (*sOrigStore32)(struct ARMCore *, uint32_t, int32_t, int *);
static void (*sOrigStore16)(struct ARMCore *, uint32_t, int16_t, int *);
static void (*sOrigStore8)(struct ARMCore *, uint32_t, int8_t, int *);
static uint32_t (*sOrigStoreMultiple)(struct ARMCore *, uint32_t, int, enum LSMDirection, int *);
static void (*sOrigDrawScanline)(struct GBAVideoRenderer *, int);

/* I/O registers a raster region may cover: 0x04000000 up to REG_IME (REG_IE included). */
#define RASTER_IO_END 0x04000208u

/* A GBA address in its canonical mirror, or 0 when it is not in a watched kind of memory. */
static uint32_t RasterCanon(uint32_t a)
{
    switch (a >> 24)
    {
    case 2: return 0x02000000u | (a & 0x3FFFFu);
    case 3: return 0x03000000u | (a & 0x7FFFu);
    case 4: return a < RASTER_IO_END ? a : 0; /* not mirrored here; nothing from REG_IME up */
    case 5: return 0x05000000u | (a & 0x3FFu);
    case 6:
    {
        uint32_t off = a & 0x1FFFFu;
        return 0x06000000u | (off >= 0x18000u ? off - 0x8000u : off); /* 0x06018000-: the OBJ half again */
    }
    case 7: return 0x07000000u | (a & 0x3FFu);
    }
    return 0;
}

/* The emulator's bytes at canonical address a. */
static uint8_t *RasterHost(uint32_t a)
{
    struct GBA *gba = (struct GBA *)sCore->board;

    switch (a >> 24)
    {
    case 2: return (uint8_t *)gba->memory.wram + (a & 0x3FFFFu);
    case 3: return (uint8_t *)gba->memory.iwram + (a & 0x7FFFu);
    case 4: return (uint8_t *)gba->memory.io + (a & 0x3FFu); /* mGBA's register image, uint16_t io[512] */
    case 5: return (uint8_t *)gba->video.palette + (a & 0x3FFu);
    case 6: return (uint8_t *)gba->video.vram + (a & 0x1FFFFu);
    case 7: return (uint8_t *)gba->video.oam.raw + (a & 0x3FFu);
    }
    return NULL;
}

static struct RasterRegion *RasterRegionOf(uint32_t canon)
{
    unsigned i;

    for (i = 0; i < sRrCount; i++)
        if (canon >= sRr[i].addr && canon - sRr[i].addr < sRr[i].size)
            return &sRr[i];
    return NULL;
}

/*
 * The bits of a watched halfword that the log keeps. REG_DISPSTAT's bits 0-2
 * are the video unit's status flags (VBlank, HBlank, VCount match): mGBA's
 * register image changes them on every line without a CPU store, so they are
 * left out of every logged value, of the self-check and of the region's hash
 * (the PC's frame step sets its own). Everything else is kept whole.
 */
static uint16_t RasterKeep(uint32_t canon)
{
    return canon == 0x04000004u ? 0xFFF8u : 0xFFFFu;
}

/* The watched halfword at canonical a, as the log keeps it. */
static uint16_t RasterRead16(uint32_t a)
{
    uint16_t v;

    memcpy(&v, RasterHost(a), 2);
    return (uint16_t)(v & RasterKeep(a));
}

/* Region r's bytes now, as the log keeps them, into buf. */
static void RasterLive(const struct RasterRegion *r, uint8_t *buf)
{
    uint32_t k;

    memcpy(buf, RasterHost(r->addr), r->size);
    if ((r->addr >> 24) != 4)
        return;
    for (k = 0; k + 1 < r->size; k += 2)
    {
        uint16_t v = RasterRead16(r->addr + k);

        memcpy(buf + k, &v, 2);
    }
}

static void RasterCopy(struct RasterRegion *r)
{
    RasterLive(r, r->copy);
    r->copied = 1;
}

/* The frame reaches VCOUNT 0-159: video stores are logged from here on. */
static void RasterVideoStart(void)
{
    unsigned i;

    if (sRasterVideoOn)
        return;
    sRasterVideoOn = 1;
    for (i = 0; i < sRrCount; i++)
        if (sRr[i].video)
            RasterCopy(&sRr[i]);
}

static int RasterVcount(void)
{
    return ((struct GBA *)sCore->board)->video.vcount;
}

/* The watched halfwords of [a, a + n) (n even, a even), before a store. */
#define RASTER_WINDOW 68
struct RasterSave
{
    unsigned count;
    uint32_t addr[RASTER_WINDOW];
    uint16_t value[RASTER_WINDOW];
};

/* Does canonical [lo, hi) touch a watched region? */
static int RasterRangeWatched(uint32_t lo, uint32_t hi)
{
    unsigned i;

    for (i = 0; i < sRrCount; i++)
        if (sRr[i].addr < hi && lo < sRr[i].addr + sRr[i].size)
            return 1;
    return 0;
}

static void RasterBefore(uint32_t a, unsigned n, struct RasterSave *save)
{
    unsigned i;
    uint32_t c0, c1;

    save->count = 0;
    if (sRasterLog == NULL)
        return;
    /* Most stores (the stack, most of EWRAM) touch no watched region. When
       the halfwords' canonical addresses run on from c0 without a break (one
       kind of memory, no mirror wrap, no fold of VRAM's upper 32 KiB), they
       are [c0, c1]; if no region touches that, the loop below keeps nothing
       and calls nothing, so it is skipped (runner speed, review rv-h8). */
    c0 = RasterCanon(a);
    c1 = RasterCanon(a + n - 2);
    if (c0 != 0 && c1 - c0 == n - 2 && !RasterRangeWatched(c0, c1 + 2))
        return;
    for (i = 0; i < n; i += 2)
    {
        uint32_t c = RasterCanon(a + i);
        struct RasterRegion *r;

        if (c == 0 || (r = RasterRegionOf(c)) == NULL)
            continue;
        if (r->video)
        {
            if (RasterVcount() >= 160)
                continue; /* shows on every line of the next frame drawn */
            RasterVideoStart();
        }
        save->addr[save->count] = c;
        save->value[save->count] = RasterRead16(c);
        save->count++;
    }
}

static void RasterAfter(struct ARMCore *cpu, const struct RasterSave *save)
{
    unsigned i;

    for (i = 0; i < save->count; i++)
    {
        uint16_t now;
        struct RasterStore *s;

        now = RasterRead16(save->addr[i]);
        if (now == save->value[i])
            continue;
        if (sRsCount == sRsCap)
        {
            sRsCap = sRsCap ? 2 * sRsCap : 4096;
            sRs = realloc(sRs, sRsCap * sizeof(*sRs));
            if (sRs == NULL)
            {
                fprintf(stderr, "mgba_lua_run: out of memory for the raster log\n");
                exit(1);
            }
        }
        s = &sRs[sRsCount++];
        s->addr = save->addr[i];
        s->oldValue = save->value[i];
        s->newValue = now;
        s->pos = (uint16_t)(sRevCount + sRevLost < 0xFFFF ? sRevCount + sRevLost : 0xFFFF);
        s->flags = (uint16_t)(sRasterLineHandlers > 0 ? 1 : 0);
        s->pc = (uint32_t)cpu->gprs[ARM_PC] - (cpu->cpsr.t ? 2 * WORD_SIZE_THUMB : 2 * WORD_SIZE_ARM);
    }
}

static int RasterPage(uint32_t a)
{
    uint32_t p = a >> 24;

    return p == 2 || p == 3 || p == 4 || p == 5 || p == 6 || p == 7;
}

static void RasterStore32(struct ARMCore *cpu, uint32_t address, int32_t value, int *cycleCounter)
{
    struct RasterSave save;

    if (!RasterPage(address))
    {
        sOrigStore32(cpu, address, value, cycleCounter);
        return;
    }
    RasterBefore(address & ~3u, 4, &save);
    sOrigStore32(cpu, address, value, cycleCounter);
    RasterAfter(cpu, &save);
}

static void RasterStore16(struct ARMCore *cpu, uint32_t address, int16_t value, int *cycleCounter)
{
    struct RasterSave save;

    if (!RasterPage(address))
    {
        sOrigStore16(cpu, address, value, cycleCounter);
        return;
    }
    RasterBefore(address & ~1u, 2, &save);
    sOrigStore16(cpu, address, value, cycleCounter);
    RasterAfter(cpu, &save);
}

static void RasterStore8(struct ARMCore *cpu, uint32_t address, int8_t value, int *cycleCounter)
{
    struct RasterSave save;

    if (!RasterPage(address))
    {
        sOrigStore8(cpu, address, value, cycleCounter);
        return;
    }
    RasterBefore(address & ~1u, 2, &save); /* a byte store to palette RAM or BG VRAM writes the halfword */
    sOrigStore8(cpu, address, value, cycleCounter);
    RasterAfter(cpu, &save);
}

/* STM: at most 16 words, above or below the base (the four directions):
   the halfwords of [base - 64, base + 68) cover all of them. */
static uint32_t RasterStoreMultiple(struct ARMCore *cpu, uint32_t baseAddress, int mask,
                                    enum LSMDirection direction, int *cycleCounter)
{
    struct RasterSave save;
    uint32_t ret, first = (baseAddress & ~3u) - 64;

    if (!RasterPage(baseAddress))
        return sOrigStoreMultiple(cpu, baseAddress, mask, direction, cycleCounter);
    RasterBefore(first, 2 * RASTER_WINDOW, &save);
    ret = sOrigStoreMultiple(cpu, baseAddress, mask, direction, cycleCounter);
    RasterAfter(cpu, &save);
    return ret;
}

static void RasterEvent(int kind, int line)
{
    if (line == 160)
        return; /* before the PC's frame step after the sample (lines 161-227, 0-159) */
    if (line < 160)
        RasterVideoStart();
    if (sRevCount < RASTER_MAX_EVENTS)
        sRev[sRevCount++] = (uint16_t)(kind << 8 | (line & 0xFF));
    else
        sRevLost++;
}

static void RasterDrawScanline(struct GBAVideoRenderer *renderer, int y)
{
    if (sRasterLog != NULL)
        RasterEvent(2, y);
    sOrigDrawScanline(renderer, y);
}

/* The run loop, around each instruction: IrqMain calling a handler, or the
   handler back at post_irq. An IRQ can be taken before either instruction
   (IrqMain runs the call in System mode with IRQs on), and then the CPU comes
   back to it later: so each counts only when the step ran it (the next
   instruction is the handler's first, or the one after post_irq's). */
static uint32_t sRasterPendingTarget, sRasterPendingAfter;
static int sRasterPendingIndex = -1, sRasterPendingPop;

static void RasterPreStep(struct ARMCore *cpu, uint32_t next)
{
    sRasterPendingIndex = -1;
    sRasterPendingPop = 0;
    if (next == sRasterIrqCall[0] || next == sRasterIrqCall[1])
    {
        sRasterPendingIndex = (int)cpu->gprs[2] / 4; /* IrqMain's r2: 4 * IRQ number (0 VBlank, 1 HBlank, 2 VCount) */
        sRasterPendingTarget = (uint32_t)cpu->gprs[0] & ~1u;
    }
    else if (next == sRasterPostIrq[0] || next == sRasterPostIrq[1])
    {
        sRasterPendingPop = 1;
        sRasterPendingAfter = next + 4; /* post_irq's ldmia is ARM code */
    }
}

static void RasterPostStep(struct ARMCore *cpu)
{
    uint32_t cur = cpu->gprs[ARM_PC] - (cpu->cpsr.t ? WORD_SIZE_THUMB : WORD_SIZE_ARM);
    int max = (int)(sizeof(sRasterIrqStack) / sizeof(sRasterIrqStack[0]));

    if (sRasterPendingIndex >= 0 && cur == sRasterPendingTarget)
    {
        int index = sRasterPendingIndex;

        if (sRasterIrqDepth < max)
            sRasterIrqStack[sRasterIrqDepth] = index;
        sRasterIrqDepth++;
        if (index == 1 || index == 2)
        {
            RasterEvent(index == 2 ? 1 : 3, RasterVcount());
            sRasterLineHandlers++;
        }
    }
    else if (sRasterPendingPop && cur == sRasterPendingAfter && sRasterIrqDepth > 0)
    {
        int index;

        sRasterIrqDepth--;
        index = sRasterIrqDepth < max ? sRasterIrqStack[sRasterIrqDepth] : 0;
        if ((index == 1 || index == 2) && sRasterLineHandlers > 0)
            sRasterLineHandlers--;
    }
    sRasterPendingIndex = -1;
    sRasterPendingPop = 0;
}

/* After each "frame" callback: the next frame starts (line 160). */
static void RasterFrameReset(void)
{
    unsigned i;

    sRsCount = 0;
    sRevCount = 0;
    sRevLost = 0;
    sRasterVideoOn = 0;
    for (i = 0; i < sRrCount; i++)
    {
        sRr[i].copied = 0;
        if (!sRr[i].video)
            RasterCopy(&sRr[i]); /* RAM ranges: every store of the frame is logged */
    }
}

/* The self-check: region = its copy with the logged stores applied, in order. */
static int RasterRegionExplained(struct RasterRegion *r)
{
    size_t i;

    if (!r->copied)
        RasterCopy(r); /* a video region of a frame that never reached line 0: nothing logged */
    for (i = 0; i < sRsCount; i++)
    {
        const struct RasterStore *s = &sRs[i];
        uint16_t old;

        if (s->addr < r->addr || s->addr - r->addr >= r->size)
            continue;
        memcpy(&old, r->copy + (s->addr - r->addr), 2);
        if (old != s->oldValue)
            return 0;
        memcpy(r->copy + (s->addr - r->addr), &s->newValue, 2);
    }
    if ((r->addr >> 24) == 4)
    {
        uint8_t live[RASTER_IO_END - 0x04000000u];

        RasterLive(r, live); /* an I/O region: in the bits the log keeps */
        return memcmp(r->copy, live, r->size) == 0;
    }
    return memcmp(r->copy, RasterHost(r->addr), r->size) == 0;
}

static uint32_t sRasterCrcTable[256];

static uint32_t RasterCrc(uint32_t crc, const void *p, size_t n)
{
    const uint8_t *b = p;

    if (sRasterCrcTable[1] == 0)
    {
        unsigned i, k;
        for (i = 0; i < 256; i++)
        {
            uint32_t v = i;
            for (k = 0; k < 8; k++)
                v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            sRasterCrcTable[i] = v;
        }
    }
    crc = ~crc;
    while (n--)
        crc = sRasterCrcTable[(crc ^ *b++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

static uint32_t sRasterBlockCrc;

static void RasterPut(const void *p, size_t n)
{
    fwrite(p, 1, n, sRasterLog);
    sRasterBlockCrc = RasterCrc(sRasterBlockCrc, p, n);
}

static double RunnerRasterWrite(int32_t tick)
{
    uint32_t head[4], u;
    unsigned i;
    int incomplete = sRevLost != 0;

    if (sRasterLog == NULL)
        return -1;
    for (i = 0; i < sRrCount; i++)
        if (!RasterRegionExplained(&sRr[i]))
            incomplete = 1;
    sRasterBlockCrc = 0;
    head[0] = 0x31425252u; /* "RRB1" */
    head[1] = (uint32_t)tick;
    head[2] = incomplete ? 1u : 0u;
    head[3] = sRrCount;
    RasterPut(head, sizeof(head));
    for (i = 0; i < sRrCount; i++)
    {
        uint8_t live[RASTER_IO_END - 0x04000000u];
        const uint8_t *m = RasterHost(sRr[i].addr);
        uint32_t rec[3], h = 0x811C9DC5u, k;

        if ((sRr[i].addr >> 24) == 4)
        {
            RasterLive(&sRr[i], live); /* an I/O region: in the bits the log keeps */
            m = live;
        }
        for (k = 0; k < sRr[i].size; k++)
            h = (h ^ m[k]) * 0x01000193u;
        rec[0] = sRr[i].addr;
        rec[1] = sRr[i].size;
        rec[2] = h;
        RasterPut(sRr[i].name, sizeof(sRr[i].name));
        RasterPut(rec, sizeof(rec));
    }
    u = sRevCount;
    RasterPut(&u, 4);
    RasterPut(sRev, 2 * sRevCount);
    if (sRevCount & 1)
    {
        uint16_t pad = 0;
        RasterPut(&pad, 2);
    }
    u = (uint32_t)sRsCount;
    RasterPut(&u, 4);
    RasterPut(sRs, sRsCount * sizeof(*sRs));
    fflush(sRasterLog);
    sRasterBlocks++;
    if (incomplete)
        sRasterIncomplete++;
    return (double)sRasterBlockCrc;
}

mSCRIPT_BIND_FUNCTION(sBindRasterWrite, F64, RunnerRasterWrite, 1, S32, tick);

static void RasterStats(void)
{
    if (sRasterLog != NULL)
        fprintf(stderr, "mgba_lua_run: raster log: %lu frames written, %lu of them incomplete\n", sRasterBlocks,
            sRasterIncomplete);
}

/* MGBA_RUN_RASTER_RAM="name@0xADDR+0xSIZE,...": the RAM ranges line handlers read, and the I/O
   registers that decide which of them run (REG_IE). */
static int RasterAddRegion(const char *name, uint32_t addr, uint32_t size, int video)
{
    struct RasterRegion *r;

    if (sRrCount == RASTER_MAX_REGIONS || strlen(name) >= sizeof(r->name) || size == 0 || (addr & 1) || (size & 1)
        || RasterCanon(addr) != addr || RasterCanon(addr + size - 1) != addr + size - 1
        || (addr >> 24) != ((addr + size - 1) >> 24))
        return 0;
    r = &sRr[sRrCount++];
    memset(r, 0, sizeof(*r));
    strcpy(r->name, name);
    r->addr = addr;
    r->size = size;
    r->video = video;
    r->copy = malloc(size);
    return r->copy != NULL;
}

static int RasterInstall(struct mCore *core, int haveBreakpoints)
{
    const char *path = getenv("MGBA_RUN_RASTERLOG"), *ram = getenv("MGBA_RUN_RASTER_RAM");
    const char *irq = getenv("MGBA_RUN_RASTER_IRQ");
    struct ARMCore *cpu = core->cpu;
    struct GBA *gba = (struct GBA *)core->board;
    uint32_t head[2];
    char buf[1024], *item, *save = NULL;

    if (path == NULL || *path == '\0')
        return 1;
    if (!haveBreakpoints)
    {
        fprintf(stderr, "mgba_lua_run: MGBA_RUN_RASTERLOG needs breakpoints (MGBA_RUN_BP): it steps one instruction "
                        "at a time\n");
        return 0;
    }
    if (irq == NULL || sscanf(irq, "%i,%i", (int *)&sRasterIrqCall[0], (int *)&sRasterIrqCall[1]) != 2)
    {
        fprintf(stderr, "mgba_lua_run: MGBA_RUN_RASTERLOG needs MGBA_RUN_RASTER_IRQ=0xROM,0xIWRAM (IrqMain's bx r0)\n");
        return 0;
    }
    /* IrqMain (crt0.s): `bx r0` is followed by post_irq, where the handler returns. */
    sRasterPostIrq[0] = sRasterIrqCall[0] + 4;
    sRasterPostIrq[1] = sRasterIrqCall[1] + 4;
    if (!RasterAddRegion("pltt", 0x05000000u, 0x400u, 1) || !RasterAddRegion("vram", 0x06000000u, 0x18000u, 1)
        || !RasterAddRegion("oam", 0x07000000u, 0x400u, 1))
        return 0;
    if (ram != NULL && *ram != '\0')
    {
        if (strlen(ram) >= sizeof(buf))
            return 0;
        strcpy(buf, ram);
        for (item = strtok_r(buf, ",", &save); item != NULL; item = strtok_r(NULL, ",", &save))
        {
            char *at = strchr(item, '@'), *plus;
            uint32_t addr, size;

            if (at == NULL || (plus = strchr(at, '+')) == NULL)
                break;
            *at = '\0';
            addr = (uint32_t)strtoul(at + 1, NULL, 0);
            size = (uint32_t)strtoul(plus + 1, NULL, 0);
            if ((addr >> 24) != 2 && (addr >> 24) != 3 && (addr >> 24) != 4)
                break;
            if (!RasterAddRegion(item, addr, size, 0))
                break;
        }
        if (item != NULL)
        {
            fprintf(stderr, "mgba_lua_run: bad MGBA_RUN_RASTER_RAM (want name@0xADDR+0xSIZE,... in EWRAM, "
                            "IWRAM or I/O below 0x04000208, even, at most %d regions)\n",
                    RASTER_MAX_REGIONS - RASTER_VIDEO_REGIONS);
            return 0;
        }
    }
    sRasterLog = fopen(path, "wb");
    if (sRasterLog == NULL)
    {
        fprintf(stderr, "mgba_lua_run: cannot write MGBA_RUN_RASTERLOG %s\n", path);
        return 0;
    }
    head[0] = 0x31465252u; /* "RRF1" */
    head[1] = 0;
    fwrite(head, sizeof(head), 1, sRasterLog);
    sOrigStore32 = cpu->memory.store32;
    sOrigStore16 = cpu->memory.store16;
    sOrigStore8 = cpu->memory.store8;
    sOrigStoreMultiple = cpu->memory.storeMultiple;
    cpu->memory.store32 = RasterStore32;
    cpu->memory.store16 = RasterStore16;
    cpu->memory.store8 = RasterStore8;
    cpu->memory.storeMultiple = RasterStoreMultiple;
    sOrigDrawScanline = gba->video.renderer->drawScanline;
    gba->video.renderer->drawScanline = RasterDrawScanline;
    RasterFrameReset();
    atexit(RasterStats);
    fprintf(stderr, "mgba_lua_run: raster log to %s (%u regions)\n", path, sRrCount);
    return 1;
}

static uint32_t FileCrc32(const char *path)
{
    static uint32_t table[256];
    uint8_t buf[65536];
    uint32_t crc = 0xFFFFFFFFu;
    size_t n;
    unsigned i, k;
    FILE *f = fopen(path, "rb");

    for (i = 0; i < 256; i++)
    {
        uint32_t v = i;
        for (k = 0; k < 8; k++)
            v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1;
        table[i] = v;
    }
    if (f == NULL)
        return 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        for (i = 0; i < n; i++)
            crc = table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    fclose(f);
    return crc ^ 0xFFFFFFFFu;
}

int main(int argc, char **argv)
{
    struct mScriptContext script;
    struct mScriptEngineContext *lua;
    struct VFile *vf;
    struct mCore *core;
    unsigned width, height;
    color_t *video;
    unsigned long maxFrames, i;
    struct mDebugger debugger;
    const char *stateIn;
    int b;

    if (argc != 4)
    {
        fprintf(stderr, "usage: %s ROM SCRIPT.lua MAX_FRAMES\n", argv[0]);
        return 2;
    }
    maxFrames = strtoul(argv[3], NULL, 0);
    if (!ParseBreakpoints(getenv("MGBA_RUN_BP")))
    {
        fprintf(stderr, "mgba_lua_run: bad MGBA_RUN_BP (want name@0xADDR[,name@0xADDR...], at most %d)\n",
            MAX_BREAKPOINTS);
        return 2;
    }

    mLogSetDefaultLogger(&sLogger);

    core = mCoreFind(argv[1]);
    if (core == NULL || !core->init(core))
    {
        fprintf(stderr, "mgba_lua_run: no core for %s\n", argv[1]);
        return 1;
    }
    /* Config with defaults only: the user's mGBA config (BIOS path, audio,
       colour correction) is never read. */
    mCoreInitConfig(core, NULL);
    mCoreConfigSetDefaultIntValue(&core->config, "useBios", 0);
    mCoreConfigSetDefaultIntValue(&core->config, "skipBios", 1);
    mCoreLoadConfig(core);

    core->desiredVideoDimensions(core, &width, &height);
    video = calloc((size_t)width * height, sizeof(*video));
    core->setVideoBuffer(core, video, width);

    if (!mCoreLoadFile(core, argv[1]))
    {
        fprintf(stderr, "mgba_lua_run: cannot load %s\n", argv[1]);
        return 1;
    }
    if (!LoadSramIn(core))
        return 1;
    core->reset(core);
    sCore = core;
    stateIn = getenv("MGBA_RUN_STATE_IN");
    if (stateIn != NULL && *stateIn != '\0')
    {
        struct VFile *svf = VFileOpen(stateIn, O_RDONLY);

        if (svf == NULL || !mCoreLoadStateNamed(core, svf, SAVESTATE_SAVEDATA))
        {
            fprintf(stderr, "mgba_lua_run: cannot load the save state %s\n", stateIn);
            return 1;
        }
        svf->close(svf);
        fprintf(stderr, "mgba_lua_run: save state %s loaded (frame %u)\n", stateIn, core->frameCounter(core));
    }
    else
        stateIn = "";

    mScriptContextInit(&script);
    mScriptContextAttachStdlib(&script);
    mScriptContextAttachLogger(&script, &sLogger);
    lua = mScriptContextRegisterEngine(&script, mSCRIPT_ENGINE_LUA);
    mScriptContextAttachCore(&script, core);
    mScriptContextSetGlobal(&script, "runner_save_state", (struct mScriptValue *)&sBindSaveState);
    mScriptContextSetGlobal(&script, "runner_cycles", (struct mScriptValue *)&sBindCycles);
    mScriptContextSetGlobal(&script, "runner_irq_sp", (struct mScriptValue *)&sBindIrqSp);
    mScriptContextSetGlobal(&script, "runner_irq_reg", (struct mScriptValue *)&sBindIrqReg);
    mScriptContextSetGlobal(&script, "runner_raster_write", (struct mScriptValue *)&sBindRasterWrite);
    {
        struct mScriptValue *v = mScriptStringCreateFromUTF8(stateIn);
        mScriptContextSetGlobal(&script, "runner_state_loaded", v);
        mScriptValueDeref(v);
        v = mScriptValueCreateFromUInt(FileCrc32(argv[1]));
        mScriptContextSetGlobal(&script, "runner_rom_crc32", v);
        mScriptValueDeref(v);
    }
    /* In 0.10 mScriptContextLoadFile only compiles the script; the frontend
       then calls the engine's run(), as the Qt ScriptingController does. */
    vf = VFileOpen(argv[2], O_RDONLY);
    if (lua == NULL || vf == NULL || !lua->load(lua, argv[2], vf))
    {
        fprintf(stderr, "mgba_lua_run: cannot load script %s: %s\n", argv[2],
            (lua != NULL && lua->getError(lua) != NULL) ? lua->getError(lua) : "?");
        return 1;
    }
    if (!lua->run(lua))
    {
        fprintf(stderr, "mgba_lua_run: script %s failed: %s\n", argv[2],
            lua->getError(lua) != NULL ? lua->getError(lua) : "?");
        return 1;
    }

    /* A custom debugger: no CLI or GDB stub, only the breakpoints above. */
    memset(&debugger, 0, sizeof(debugger));
    if (sBreakCount > 0)
    {
        debugger.type = DEBUGGER_CUSTOM;
        debugger.entered = OnDebuggerEntered;
        mDebuggerAttach(&debugger, core);
        for (b = 0; b < sBreakCount; b++)
        {
            struct mBreakpoint bp = { .address = sBreak[b].address, .segment = -1,
                                      .type = BREAKPOINT_HARDWARE };
            if (debugger.platform->setBreakpoint(debugger.platform, &bp) < 0)
            {
                fprintf(stderr, "mgba_lua_run: cannot set breakpoint %s at 0x%08X\n",
                    sBreak[b].name, sBreak[b].address);
                return 1;
            }
            fprintf(stderr, "mgba_lua_run: breakpoint %s at 0x%08X%s\n", sBreak[b].name,
                sBreak[b].address, sBreak[b].retName[0] != '\0' ? " (returns reported)" : "");
        }
        if (!RunnerStepCheck(&debugger))
            return 1;
        atexit(PrintBreakStats);
    }
    /* After the debugger, so its memory functions are the ones wrapped. */
    if (!RasterInstall(core, sBreakCount > 0))
        return 1;

    /* One instruction per RunnerStep. The debugger reports a breakpoint when
       the next instruction is at its address. If an IRQ is taken there before
       that instruction runs, the IRQ handler returns to the same instruction
       and the debugger reports it again: that second report is the same call,
       so it is counted in `rehits` and not passed to the script. An IRQ taken
       shows as the step that switches the CPU into IRQ mode. At that step the
       runner also fires the callback "<name>_irq", and it fires
       "<name>_resume" for the repeated report, when the CPU is back at the
       instruction (a script that registers them learns that the call it was
       told about had not run its instruction while the IRQ ran;
       gate0_capture.lua uses this for the lag position).
       The repeated report is the one made with the interrupted code's stack
       pointer and CPU mode, kept at the IRQ step. The IRQ handler can reach
       the same address first: FE8's IrqMain (crt0.s) calls the handler in
       System mode on the main loop's stack, so OnVBlank's Proc_Run of tree 0
       reaches RunProcessRecursive and RunProcessScript lower on that stack.
       Such a report is a call of its own and goes to the script as one.
       (Until 2026-10-20 the first report at the address after the IRQ was
       taken for the repeat: on route ch1 at ticks 6,541 and 11,912 the IRQ
       came at RunProcessScript's first instruction, tree 0's call was
       dropped, and the main loop's call was reported twice.) */
    for (i = 0; i < maxFrames; i++)
    {
        if (sBreakCount == 0)
            core->runFrame(core);
        else
        {
            struct ARMCore *cpu = core->cpu;
            uint32_t frame = core->frameCounter(core);
            while (core->frameCounter(core) == frame)
            {
                uint32_t next = cpu->gprs[ARM_PC] - (cpu->cpsr.t ? WORD_SIZE_THUMB : WORD_SIZE_ARM);
                int modeBefore = cpu->cpsr.priv;
                uint32_t spBefore = (uint32_t)cpu->gprs[ARM_SP];

                if (sRasterLog != NULL)
                    RasterPreStep(cpu, next);
                RunnerStep(&debugger, core, cpu);
                if (sRasterLog != NULL)
                    RasterPostStep(cpu);
                if (cpu->cpsr.priv == MODE_IRQ && modeBefore != MODE_IRQ)
                {
                    IrqEntryRecord(cpu, modeBefore); /* runner_irq_reg */
                    for (b = 0; b < sBreakCount; b++)
                        if (sBreak[b].address == next)
                        {
                            if (sBreak[b].irqBeforeCount == MAX_IRQ_BEFORE)
                            {
                                fprintf(stderr, "mgba_lua_run: more than %d IRQs pending at %s\n",
                                    MAX_IRQ_BEFORE, sBreak[b].name);
                                exit(1);
                            }
                            sBreak[b].irqBefore[sBreak[b].irqBeforeCount].sp = spBefore;
                            sBreak[b].irqBefore[sBreak[b].irqBeforeCount].mode = modeBefore;
                            sBreak[b].irqBeforeCount++;
                            mScriptContextTriggerCallback(&script, sBreak[b].irqName);
                        }
                }
                if (sOpenCount > 0)
                {
                    uint32_t cur = cpu->gprs[ARM_PC] - (cpu->cpsr.t ? WORD_SIZE_THUMB : WORD_SIZE_ARM);

                    if (cur == sOpen[sOpenCount - 1].ret && (uint32_t)cpu->gprs[ARM_SP] == sOpen[sOpenCount - 1].sp)
                    {
                        b = sOpen[--sOpenCount].bp;
                        sBreak[b].returns++;
                        mScriptContextTriggerCallback(&script, sBreak[b].retName);
                    }
                }
                if (sBreakHit >= 0)
                {
                    unsigned n;

                    b = sBreakHit;
                    sBreakHit = -1;
                    n = sBreak[b].irqBeforeCount;
                    if (n > 0 && sBreak[b].irqBefore[n - 1].sp == (uint32_t)cpu->gprs[ARM_SP] &&
                        sBreak[b].irqBefore[n - 1].mode == (int)cpu->cpsr.priv)
                    {
                        sBreak[b].irqBeforeCount--;
                        sBreak[b].rehits++;
                        mScriptContextTriggerCallback(&script, sBreak[b].resumeName);
                    }
                    else
                    {
                        sBreak[b].hits++;
                        if (sBreak[b].retName[0] != '\0')
                        {
                            if (sOpenCount == MAX_OPEN_CALLS)
                            {
                                fprintf(stderr, "mgba_lua_run: more than %d open calls at %s\n",
                                    MAX_OPEN_CALLS, sBreak[b].name);
                                exit(1);
                            }
                            sOpen[sOpenCount].bp = b;
                            sOpen[sOpenCount].ret = cpu->gprs[ARM_LR] & ~1u;
                            sOpen[sOpenCount].sp = (uint32_t)cpu->gprs[ARM_SP];
                            sOpenCount++;
                        }
                        mScriptContextTriggerCallback(&script, sBreak[b].name);
                    }
                }
            }
        }
        mScriptContextTriggerCallback(&script, "frame");
        if (sRasterLog != NULL)
            RasterFrameReset(); /* the raster log holds one frame (runner_raster_write) */
    }
    fprintf(stderr, "mgba_lua_run: frame limit %lu reached\n", maxFrames);

    mScriptContextDeinit(&script);
    core->deinit(core);
    free(video);
    return 3;
}
