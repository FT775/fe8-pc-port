#define _GNU_SOURCE
#include "global.h"

#include <execinfo.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

#include "hardware.h"
#include "proc.h"
#include "bm.h"

#include "port_hw.h"
#include "render.h"
#include "../platform.h"
#include "../audio/audio_pc.h"

/*
 * Host entry point. Replaces src/crt0.s: reserve the GBA memory map, load the
 * user's ROM, then call AgbMain exactly as the GBA reset vector does.
 */

void AgbMain(void);
void __real_OnMain(void);

uint32_t gPortOnMainCalls;
static unsigned sStubsHit;
static jmp_buf sResetJmp;

void PortLog(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fflush(stdout);
}

static int IsReadable(const void *p)
{
    uint32_t a = (uint32_t)(uintptr_t)p;
    return (a >= PORT_ROM_START && a < PORT_ROM_START + PORT_ROM_MAX) ||
           (a >= 0x10000000u && a < 0x20000000u);
}

static void DumpProc(ProcPtr ptr)
{
    struct Proc *proc = ptr;
    const char *name = IsReadable(proc->proc_name) ? proc->proc_name : "(no name)";

    PortLog("[state]   proc %p script %p name %s\n", (void *)proc, (void *)proc->proc_script, name);
}

/* Summary of what the game logic has done, for SPIKE.md. */
static void DumpState(void)
{
    const uint8_t *vram = (const uint8_t *)(uintptr_t)PORT_VRAM_START;
    const uint16_t *pltt = (const uint16_t *)(uintptr_t)PORT_PLTT_START;
    uint32_t i, vramNonZero = 0, plttNonZero = 0, hash = 2166136261u;

    for (i = 0; i < 0x18000; i++)
    {
        vramNonZero += vram[i] != 0;
        hash = (hash ^ vram[i]) * 16777619u;
    }
    for (i = 0; i < 0x200; i++)
        plttNonZero += pltt[i] != 0;
    PortLog("[state] gGameClock=%u DISPCNT=0x%04X gMainCallback=%p\n", GetGameClock(),
        REG_DISPCNT, (void *)gMainCallback);
    PortLog("[state] VRAM non-zero bytes=%u (FNV-1a 0x%08X), PLTT non-zero entries=%u\n",
        vramNonZero, hash, plttNonZero);
    PortLog("[state] live procs:\n");
    Proc_ForAll(DumpProc);
}

void PortExit(int code)
{
    PortSave_Flush(); /* the save file first, before anything else can fail */
    PortLog("[port] exit %d: frames=%u OnMain calls=%u distinct stubs hit=%u\n", code,
        gPortFrameCount, gPortOnMainCalls, sStubsHit);
    PortPaceReport();
    PortAudio_Report();
    DumpState();
    Platform_Shutdown();
    exit(code);
}

/* Called by the generated stubs in autostubs.c. */
void PortStubHit(const char *name, unsigned gbaAddr, unsigned *count)
{
    if ((*count)++ == 0)
    {
        sStubsHit++;
        PortLog("[stub] %-40s GBA 0x%08X  (first call, frame %u)\n", name, gbaAddr,
            gPortFrameCount);
    }
}

/* Linked with --wrap=OnMain: every reference outside bm.o goes through here. */
void __wrap_OnMain(void)
{
    if (gPortOnMainCalls++ == 0)
    {
        PortLog("[port] *** first OnMain call reached (frame %u, gMainCallback=%p) ***\n",
            gPortFrameCount, (void *)gMainCallback);
    }
    __real_OnMain();
}

void PortSoftReset(void)
{
    /* REG_WAITCNT survives unless the reset flags cleared it (bios.c), so
       AgbMain tells a software reset from a power-on as the GBA does (main.c:31). */
    longjmp(sResetJmp, 1);
}

/*
 * The GBA ignores writes to the BIOS area (0x00000000-0x00003FFF), and some
 * game code writes there through NULL pointers (AnimDelete, animedrv.c:170,
 * when it unlinks the last Anim). The host cannot map page 0 (Linux
 * mmap_min_addr, Windows NULL-page protection), so such a write faults. Unless
 * FE8PC_DROP_BIOS_WRITES=0, the fault handler drops the write the way the GBA
 * does: it decodes the length of the faulting store and resumes after it. Only
 * plain integer stores in the game's own code are handled; anything else stays
 * fatal. Each site is logged once, so it can be fixed properly later.
 */
static int sDropBiosWrites = 1;
static uint32_t sDroppedSites[64];
static unsigned sDroppedCount;

static int ModRmLength(const uint8_t *p)
{
    uint8_t modrm = p[0], mod = modrm >> 6, rm = modrm & 7;
    int len = 1;

    if (mod == 3)
        return -1; /* register operand: not a memory store */
    if (rm == 4)
    {
        uint8_t sib = p[1];
        len++;
        if (mod == 0 && (sib & 7) == 5)
            len += 4;
    }
    else if (mod == 0 && rm == 5)
        len += 4;
    if (mod == 1)
        len += 1;
    else if (mod == 2)
        len += 4;
    return len;
}

/* Length of an x86 integer store to memory at p, or -1 if not handled. */
static int StoreLength(const uint8_t *p)
{
    int pre = 0, opsize16 = 0, m;
    uint8_t op, reg;

    while (p[pre] == 0x66)
    {
        opsize16 = 1;
        pre++;
    }
    op = p[pre];
    m = ModRmLength(p + pre + 1);
    if (m < 0)
        return -1;
    reg = (p[pre + 1] >> 3) & 7;
    switch (op)
    {
    case 0x88: case 0x89:                       /* mov r/m, r */
    case 0x00: case 0x01: case 0x08: case 0x09: /* add/or r/m, r */
    case 0x20: case 0x21: case 0x28: case 0x29: /* and/sub r/m, r */
    case 0x30: case 0x31:                       /* xor r/m, r */
        return pre + 1 + m;
    case 0xC6: case 0x80: case 0x83:            /* mov/alu r/m, imm8 */
        return (op == 0xC6 && reg != 0) ? -1 : pre + 1 + m + 1;
    case 0xC7: case 0x81:                       /* mov/alu r/m, imm16/32 */
        return (op == 0xC7 && reg != 0) ? -1 : pre + 1 + m + (opsize16 ? 2 : 4);
    case 0xFE: case 0xFF:                       /* inc/dec r/m */
        return reg <= 1 ? pre + 1 + m : -1;
    }
    return -1;
}

static int TryDropBiosWrite(ucontext_t *uc, uint32_t addr)
{
    uint32_t eip = (uint32_t)uc->uc_mcontext.gregs[REG_EIP];
    int isWrite = (uc->uc_mcontext.gregs[REG_ERR] & 2) != 0;
    int len;
    unsigned i;

    if (!sDropBiosWrites || !isWrite || addr >= 0x4000 || eip < 0x10000000u || eip >= 0x20000000u)
        return 0;
    len = StoreLength((const uint8_t *)(uintptr_t)eip);
    if (len <= 0)
        return 0;
    for (i = 0; i < sDroppedCount && sDroppedSites[i] != eip; i++)
        ;
    if (i == sDroppedCount && sDroppedCount < ARRAY_COUNT(sDroppedSites))
    {
        sDroppedSites[sDroppedCount++] = eip;
        fprintf(stdout, "[bios-write] write to 0x%08X dropped, as the GBA does (host EIP 0x%08X, "
                        "frame %u; first time at this site)\n", addr, eip, gPortFrameCount);
        fflush(stdout);
    }
    uc->uc_mcontext.gregs[REG_EIP] = (greg_t)(eip + len);
    return 1;
}

static void OnFault(int sig, siginfo_t *info, void *ctx)
{
    ucontext_t *uc = (ucontext_t *)ctx;
    uint32_t eip = (uint32_t)uc->uc_mcontext.gregs[REG_EIP];
    uint32_t addr = (uint32_t)(uintptr_t)info->si_addr;
    void *frames[32];
    int n;

    if (sig == SIGSEGV)
    {
        if (TryDropBiosWrite(uc, addr))
        {
            /* FE8PC_NULLPROBE=1 only: record the site (no-op otherwise). */
            PortNullProbe_NoteDroppedWrite(eip, (uint32_t)uc->uc_mcontext.gregs[REG_EIP], addr);
            return;
        }
        /* FE8PC_NULLPROBE=1 only: a BIOS-area/NULL-page access returns the
           latch (a GBA mirror reaches its real address) and the run goes on
           (port/hw/nullprobe.c). Otherwise it returns 0 at once. */
        if (PortNullProbe_OnFault(uc, addr))
            return;
    }
    /* FE8PC_NULLPROBE=1 only: an integer division trap gets the GBA libgcc
       result and the run goes on (port/hw/nullprobe.c). Otherwise it returns
       0 at once. */
    if (sig == SIGFPE && PortNullProbe_OnDivFault(uc))
        return;

    fprintf(stdout, "\n[fault] signal %d (%s) at EIP=0x%08X, fault address 0x%08X, frame %u, "
                    "OnMain calls %u\n", sig, strsignal(sig), eip, addr, gPortFrameCount,
        gPortOnMainCalls);
    if (eip >= PORT_ROM_START && eip < PORT_ROM_START + PORT_ROM_MAX)
        fprintf(stdout, "[fault] host jumped into GBA code at 0x%08X: a GBA code pointer "
                        "(ROM data or unported asm) was called\n", eip);
    fflush(stdout);
    n = backtrace(frames, 32);
    backtrace_symbols_fd(frames, n, STDOUT_FILENO);
    PortNullProbe_Dump("fatal fault"); /* FE8PC_NULLPROBE=1 only */
    _exit(3);
}

/*
 * The command line: fe8pc [--save PATH | --save=PATH | --no-save] [settings] [--] [ROM].
 * ROM defaults to baserom.gba (port/win32 also looks next to the exe). The
 * save options choose the player's save file (port/save/savefile_pc.c).
 * Settings, for this run over the settings file (the played builds' Reforged
 * Options, port/reforged/reforged_settings.c; a test build has none):
 *   --set KEY=VALUE (or --set=KEY=VALUE) any line of reforged-options.txt
 *   --scale N        window_size=N (1-8; 0 auto)
 *   --fullscreen     fullscreen=1;  --windowed: fullscreen=0
 *   --seed X         randomizer_seed=X (the next New Game's)
 *   --randomizer     randomizer=1
 */
static char sArgSetBuf[PORT_ARG_SETS][64];
static struct PortArgs sKeptArgs;

int PortArgSettingCount(void)
{
    return sKeptArgs.setCount;
}

const char *PortArgSetting(int i)
{
    return i >= 0 && i < sKeptArgs.setCount ? sKeptArgs.sets[i] : NULL;
}

/* A setting for args: as given, or made from a short form (fmt with value). */
static int AddArgSet(struct PortArgs *args, const char *fmt, const char *value, const char **error)
{
    if (args->setCount >= PORT_ARG_SETS)
    {
        *error = "too many settings on the command line";
        return -1;
    }
    if (fmt == NULL)
        args->sets[args->setCount] = value;
    else
    {
        snprintf(sArgSetBuf[args->setCount], sizeof(sArgSetBuf[0]), fmt, value != NULL ? value : "");
        args->sets[args->setCount] = sArgSetBuf[args->setCount];
    }
    args->setCount++;
    return 0;
}

int PortParseArgs(int argc, char **argv, struct PortArgs *args, const char **error)
{
    int i, options = 1;

    memset(args, 0, sizeof(*args));
    *error = NULL;
    for (i = 1; i < argc; i++)
    {
        const char *a = argv[i];

        if (options && strcmp(a, "--") == 0)
            options = 0;
        else if (options && (strcmp(a, "--set") == 0 || strcmp(a, "--scale") == 0 || strcmp(a, "--seed") == 0))
        {
            int isSet = strcmp(a, "--set") == 0, isScale = strcmp(a, "--scale") == 0;

            if (i + 1 >= argc || argv[i + 1][0] == '\0' || (isSet && strchr(argv[i + 1], '=') == NULL))
            {
                *error = isSet ? "--set needs KEY=VALUE" : isScale ? "--scale needs a number" : "--seed needs a seed";
                return -1;
            }
            i++;
            if (AddArgSet(args, isSet ? NULL : isScale ? "window_size=%s" : "randomizer_seed=%s", argv[i], error) != 0)
                return -1;
        }
        else if (options && strncmp(a, "--set=", 6) == 0)
        {
            if (strchr(a + 6, '=') == NULL)
            {
                *error = "--set= needs KEY=VALUE";
                return -1;
            }
            if (AddArgSet(args, NULL, a + 6, error) != 0)
                return -1;
        }
        else if (options && (strncmp(a, "--scale=", 8) == 0 || strncmp(a, "--seed=", 7) == 0))
        {
            if (AddArgSet(args, a[3] == 'c' ? "window_size=%s" : "randomizer_seed=%s", strchr(a, '=') + 1, error) != 0)
                return -1;
        }
        else if (options && (strcmp(a, "--fullscreen") == 0 || strcmp(a, "--windowed") == 0 ||
                             strcmp(a, "--randomizer") == 0))
        {
            if (AddArgSet(args, a[2] == 'f' ? "fullscreen=1" : a[2] == 'w' ? "fullscreen=0" : "randomizer=1", NULL,
                    error) != 0)
                return -1;
        }
        else if (options && strcmp(a, "--save") == 0)
        {
            if (i + 1 >= argc || argv[i + 1][0] == '\0')
            {
                *error = "--save needs a file path";
                return -1;
            }
            args->save = argv[++i];
        }
        else if (options && strncmp(a, "--save=", 7) == 0)
        {
            if (a[7] == '\0')
            {
                *error = "--save= needs a file path";
                return -1;
            }
            args->save = a + 7;
        }
        else if (options && strcmp(a, "--no-save") == 0)
            args->noSave = 1;
        else if (options && a[0] == '-' && a[1] != '\0')
        {
            *error = "unknown option";
            return -1;
        }
        else if (args->rom == NULL)
            args->rom = a;
        else
        {
            *error = "more than one ROM path";
            return -1;
        }
    }
    if (args->save != NULL && args->noSave)
    {
        *error = "--save and --no-save together";
        return -1;
    }
    return 0;
}

/* The Windows window build also takes --console (port/win32/gui/gui_win32.c
   takes it off the command line before main). */
#define USAGE_SETTINGS "[--set KEY=VALUE]... [--scale N] [--fullscreen | --windowed] [--seed X] [--randomizer]"
#ifdef FE8PC_WINGUI
#define USAGE_OPTIONS "[--save PATH | --no-save] [--console] " USAGE_SETTINGS " [ROM]"
#else
#define USAGE_OPTIONS "[--save PATH | --no-save] " USAGE_SETTINGS " [ROM]"
#endif

int main(int argc, char **argv)
{
    struct PortArgs args;
    const char *romPath, *argError;
    const char *limit = getenv("FE8PC_FRAMES");
    struct sigaction sa;

    if (PortParseArgs(argc, argv, &args, &argError) != 0)
    {
        fprintf(stderr, "%s: %s\nusage: %s " USAGE_OPTIONS "\n", argv[0], argError, argv[0]);
        return 1;
    }
    romPath = args.rom != NULL ? args.rom : "baserom.gba";
    sKeptArgs = args; /* the settings, for reforged_settings.c (sArgSetBuf stays put) */

    if (limit != NULL)
        gPortFrameLimit = (uint32_t)strtoul(limit, NULL, 0);
    else if (Platform_IsInteractive())
        gPortFrameLimit = 0; /* a window runs until it is closed */
    if (getenv("FE8PC_DROP_BIOS_WRITES") != NULL)
        sDropBiosWrites = strcmp(getenv("FE8PC_DROP_BIOS_WRITES"), "0") != 0;

    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = OnFault;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);

    PortLog("[port] fe8pc D1-a spike, sizeof(void*)=%u, frame limit %u\n",
        (unsigned)sizeof(void *), gPortFrameLimit);
    if (PortMemInit(romPath) != 0)
        return 1;
    /* FE8PC_NULLPROBE=1 (diagnostic): the BIOS-area/NULL-page probe. */
    if (PortNullProbe_Init() < 0)
        return 1;
    /* The player's save file into SRAM: after PortMemInit blanks it, before
       AgbMain's SramInit (main.c:55) first reads it. */
    if (PortSave_Init(romPath, args.save, args.noSave) != 0)
        return 1;
    /* After the GBA map is reserved, so nothing the backend loads lands in it. */
    if (Platform_Init() != 0)
        return 1;
    PortPaceInit(Platform_IsInteractive());
    /* PSG model, dump files, and the audio device if the backend has one. */
    PortAudio_Init();

    PortLog("[port] gMainCallback at %p, gBmSt at %p, gProcTreeRootArray at %p\n",
        (void *)&gMainCallback, (void *)&gBmSt, (void *)gProcTreeRootArray);

    if (setjmp(sResetJmp) != 0)
    {
        PortLog("[port] soft reset\n");
        gPortIrqDepth = 0; /* in case the reset came from inside an IRQ handler */
    }

    /* Renderer state is cleared at power-on and after a soft reset. */
    RenderInit();

    PortLog("[port] calling AgbMain\n");
    AgbMain();
    PortLog("[port] AgbMain returned (should never happen)\n");
    return 2;
}
