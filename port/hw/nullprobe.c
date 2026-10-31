#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port_hw.h"
#include "port_gbabus.h" /* PORT_OPEN_BUS_*: the latch words and the latch rule */

/*
 * FE8PC_NULLPROBE=1: the BIOS-area / NULL-page probe (diagnostic only; port/test
 * "nullprobe", port/tools/nullprobe.py). Unset or 0: every function here
 * returns at once and the build behaves exactly as without it.
 *
 * On the GBA a game read of 0x00000000-0x00003FFF returns the BIOS open-bus
 * latch (port-plan/DIVERGENCES.md, "Open-bus latch rule"). The host cannot map
 * the NULL page (Linux mmap_min_addr), so such a read faults, and each site
 * needs its own guarded fix (H-3). Fixing them one fault at a time is slow, so
 * this mode lets a run go past every one of them and lists them all:
 *
 *   - The SIGSEGV handler (main_pc.c) passes a fault below 0x02000000 (BIOS
 *     area, then the unused space below EWRAM) or at 0xFFFF0000 and up (small
 *     negative offsets from NULL) here. The faulting x86 instruction is
 *     decoded, the register that holds the pointer is moved by S into a shadow
 *     area at S..S+0x02000000 (the pages around the access are filled with
 *     the latch word first), the trap flag is set, and the instruction runs
 *     again. It reads the latch
 *     bytes for its own width and lanes (a byte at 0x2A gets lane 2). The
 *     single-step trap (SIGTRAP) after it moves the register back, unless the
 *     instruction overwrote it (mov %eax <- (%eax)). An absolute address with
 *     no register (mov 0x30, %eax) is emulated for plain loads and dropped for
 *     plain stores. Writes go to the shadow and are lost, as the GBA ignores
 *     them; writes that H-1 drops (main_pc.c) are only recorded.
 *   - Latch: post-IRQ 0xE55EC002 when the HBlank IRQ is on (IME, IE bit 1 and
 *     DISPSTAT bit 4), post-SWI 0xE3A02004 when only VBlank (and Game Pak)
 *     IRQs are on: the two cases the rule decides. Otherwise (inside an IRQ
 *     handler, another IRQ source on, or an address at 0x4000 and up, where the
 *     GBA returns the prefetched opcode, not the BIOS latch) the post-SWI
 *     latch, and the site is marked undecided.
 *   - A fault in a GBA memory mirror the host does not map (EWRAM above
 *     0x02040000, IWRAM above 0x03008000, palette, VRAM, OAM, ROM 0x0A-0x0D,
 *     SRAM above 0x0E010000) is run again the same way against the mirrored
 *     address, which is what the GBA reads or writes. Such a pointer usually
 *     comes from an open-bus value read earlier, so the run would stop there.
 *     A fault in unused GBA space higher up (I/O above the host's 4 KiB,
 *     0x10000000 and up where the host maps nothing) reads a shadow page with
 *     the same page offset: open bus on the GBA, marked undecided.
 *   - Each site (faulting instruction + its caller) is recorded with its
 *     addresses, read or write, first and last tick, hits, the latch rule of
 *     each hit and the callers' return addresses (backtrace). The table goes to
 *     FE8PC_NULLPROBE_OUT (JSON lines) at exit, at a fatal fault and on
 *     SIGTERM/SIGINT; each new site is also logged to stdout once, and every
 *     1,048,576 hits a progress line shows a loop that keeps faulting. The
 *     fault handler runs on its own stack in this mode, so a stack overflow
 *     is reported too.
 *   - Integer division traps (SIGFPE, PortNullProbe_OnDivFault): a game C
 *     `/` or `%` is a call of libgcc's __divsi3, __modsi3, __udivsi3 or
 *     __umodsi3 on the GBA (agbcc; fireemblem8.elf 0x080D18FC-0x080D1C0B),
 *     which never traps: a zero divisor goes to Ldiv0, which calls the empty
 *     __div0 and returns 0, and INT_MIN / -1 gives INT_MIN, remainder 0
 *     (magnitudes divided as unsigned numbers). On the host the same code is
 *     an x86 div/idiv, which raises #DE for both. A 32-bit `div`/`idiv r/m32`
 *     whose dividend is a plain 32-bit value (edx the sign of eax, or 0) gets
 *     those results in eax (quotient) and edx (remainder) and the run goes on
 *     after it; the site is recorded as "div0" or "div-overflow" (rule
 *     "div"). Anything else stays fatal. Only the quotient and remainder are
 *     the GBA's: code the host compiler arranged on the assumption that the
 *     divisor is not zero (undefined behaviour in C) can still differ.
 *
 * Other settings: FE8PC_NULLPROBE_ROUTE (label; default the GATE0_KEYS file
 * name). Ticks are the Gate 0 hook's (PortGate0Tick, port/test/pc/gate0_hook.c)
 * when it is linked, else frames. Linux i386 only; the Windows build reports
 * the mode as unsupported.
 */

#if defined(__linux__) && defined(__i386__)

#include <execinfo.h>
#include <signal.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>

#define NP_LOW_END      0x02000000u /* EWRAM starts here */
#define NP_NEG_START    0xFFFF0000u
#define NP_BIOS_END     0x00004000u
#define NP_SHADOW_SPAN  0x02000000u
#define NP_SHADOW_GUARD 0x00010000u /* below S, for the negative offsets */
#define NP_EFLAGS_TF    0x100u
#define NP_ERR_WRITE    0x2u
#define NP_ERR_FETCH    0x10u

#define NP_IO16(a) (*(volatile uint16_t *)(uintptr_t)(a))
#define NP_DISPSTAT NP_IO16(0x04000004u)
#define NP_IE       NP_IO16(0x04000200u)
#define NP_IME      NP_IO16(0x04000208u)

/* The latch choice of one hit (DIVERGENCES.md, "Open-bus latch rule"). */
enum
{
    RULE_SWI,       /* rule: only VBlank (and Game Pak) IRQs on -> post-SWI */
    RULE_HBLANK,    /* rule: HBlank IRQ on -> post-IRQ */
    RULE_IN_IRQ,    /* undecided: the read is inside an IRQ handler -> post-SWI */
    RULE_OTHER_IRQ, /* undecided: VCount, timer, DMA, serial or keypad IRQ on -> post-SWI */
    RULE_NOT_BIOS,  /* undecided: 0x4000 and up, GBA prefetch open bus -> post-SWI */
    RULE_MIRROR,    /* not open bus: a GBA memory mirror, read or written exactly */
    RULE_DIV,       /* not a memory access: an integer division trap, GBA libgcc result */
    RULE_COUNT
};
static const char *const kRuleName[RULE_COUNT] = { "swi", "hblank", "in_irq", "other_irq", "not_bios", "mirror",
                                                   "div" };

enum
{
    HOW_RELOCATE,    /* register moved into the shadow, instruction run again */
    HOW_EMULATE,     /* absolute address: load emulated or store skipped */
    HOW_H1,          /* write dropped by H-1 (main_pc.c TryDropBiosWrite), recorded only */
    HOW_UNSUPPORTED, /* not handled: the fault stays fatal */
    HOW_MIRROR,      /* register moved onto the mirrored GBA address, instruction run again */
    HOW_DIV0,        /* div/idiv by zero: GBA libgcc result (0), instruction skipped */
    HOW_DIVOVF,      /* idiv INT_MIN / -1: GBA libgcc result (INT_MIN, remainder 0), instruction skipped */
    HOW_COUNT
};
static const char *const kHowName[HOW_COUNT] = { "relocate", "emulate", "h1-drop", "unsupported", "mirror",
                                                 "div0", "div-overflow" };

#define NP_MAX_SITES 2048
#define NP_CALLERS   6

struct NpSite
{
    uint32_t eip;
    uint32_t callers[NP_CALLERS];
    uint32_t addrFirst, addrMin, addrMax;
    uint32_t firstTick, lastTick, firstFrame, lastFrame, firstOnMain;
    uint32_t hits, ruleHits[RULE_COUNT], aliasedHits, writeHits, readHits;
    uint32_t latchFirst;
    uint8_t ruleFirst, how, reg, nCallers;
    uint8_t insn[16];
    char note[64];
};

static struct NpSite sSites[NP_MAX_SITES];
static unsigned sSiteCount, sSitesLost;
static uint64_t sHits;
static int sOn;
static uint32_t sShadow;
static const char *sOutPath;
static char sRoute[128];
static const char *sKeys;

/* The instruction being single-stepped after a relocation. */
static struct
{
    int active, n;
    uint32_t eip;
    int greg[2];
    uint32_t delta[2], orig[2];
} sPend;

extern uint32_t PortGate0Tick(void) __attribute__((weak));

static uint32_t Tick(void)
{
    return PortGate0Tick != NULL ? PortGate0Tick() : gPortFrameCount;
}

static int IsLow(uint32_t a)
{
    return a < NP_LOW_END || a >= NP_NEG_START;
}

static const int kGreg[8] = { REG_EAX, REG_ECX, REG_EDX, REG_EBX, REG_ESP, REG_EBP, REG_ESI, REG_EDI };
static const char *const kRegName[8] = { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi" };

/* The latch words and the rule's post-IRQ test are port_gbabus.h's, the ones
   PortOpenBusLatch (port.h) uses for the game's guards, so a guard and the
   probe always pick the same latch; the probe only adds the undecided cases. */
static int LatchRule(uint32_t addr, uint32_t *latch)
{
    uint16_t ie = NP_IE, ime = NP_IME, dispstat = NP_DISPSTAT;
    int rule;

    if (addr >= NP_BIOS_END)
        rule = RULE_NOT_BIOS;
    else if (gPortIrqDepth != 0)
        rule = RULE_IN_IRQ;
    else if (PORT_OPEN_BUS_HBLANK_IRQ_ON(ime, ie, dispstat))
        rule = RULE_HBLANK;
    else if ((ime & 1) && ((ie & 0x1FF8) || ((ie & 0x0004) && (dispstat & 0x0020))))
        rule = RULE_OTHER_IRQ;
    else
        rule = RULE_SWI;
    *latch = rule == RULE_HBLANK ? PORT_OPEN_BUS_IRQ : PORT_OPEN_BUS_SWI;
    return rule;
}

/* ------------------------------------------------------------------------ */
/* x86 (i386) decoding: enough to find the memory operand's registers.      */

struct NpInsn
{
    int pre, op16, addr16, segFsGs, twoByte, threeByte;
    uint8_t op;
    int hasModrm, mod, reg, rm, base, index, scale;
    int32_t disp;
    int len; /* through ModRM, SIB and displacement; no immediates */
};

static int HasModrm1(uint8_t op)
{
    if (op < 0x40)
        return (op & 7) < 4;
    if (op >= 0x80 && op <= 0x8F)
        return 1;
    if (op >= 0xD8 && op <= 0xDF)
        return 1;
    switch (op)
    {
    case 0x62: case 0x63: case 0x69: case 0x6B:
    case 0xC0: case 0xC1: case 0xC4: case 0xC5: case 0xC6: case 0xC7:
    case 0xD0: case 0xD1: case 0xD2: case 0xD3:
    case 0xF6: case 0xF7: case 0xFE: case 0xFF:
        return 1;
    }
    return 0;
}

static int HasModrm2(uint8_t op)
{
    if ((op >= 0x05 && op <= 0x09) || op == 0x0B || op == 0x0E || (op >= 0x30 && op <= 0x37) ||
        op == 0x77 || (op >= 0x80 && op <= 0x8F) || (op >= 0xA0 && op <= 0xA2) ||
        (op >= 0xA8 && op <= 0xAA) || (op >= 0xC8 && op <= 0xCF))
        return 0;
    return 1;
}

static int32_t Rd32(const uint8_t *p)
{
    return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
}

static void Decode(const uint8_t *p, struct NpInsn *in)
{
    int i = 0;

    memset(in, 0, sizeof(*in));
    in->base = in->index = -1;
    in->scale = 1;
    for (; i < 14; i++)
    {
        uint8_t b = p[i];
        if (b == 0x66)
            in->op16 = 1;
        else if (b == 0x67)
            in->addr16 = 1;
        else if (b == 0x64 || b == 0x65)
            in->segFsGs = 1;
        else if (!(b == 0xF0 || b == 0xF2 || b == 0xF3 || b == 0x2E || b == 0x36 || b == 0x3E || b == 0x26))
            break;
    }
    in->pre = i;
    if (p[i] == 0x0F)
    {
        in->twoByte = 1;
        i++;
        if (p[i] == 0x38 || p[i] == 0x3A)
        {
            in->threeByte = p[i];
            i++;
            in->op = p[i++];
            in->hasModrm = 1;
        }
        else
        {
            in->op = p[i++];
            in->hasModrm = HasModrm2(in->op);
        }
    }
    else
    {
        in->op = p[i++];
        in->hasModrm = HasModrm1(in->op);
    }
    if (in->hasModrm)
    {
        uint8_t m = p[i++];
        in->mod = m >> 6;
        in->reg = (m >> 3) & 7;
        in->rm = m & 7;
        if (in->mod != 3)
        {
            if (in->rm == 4)
            {
                uint8_t sib = p[i++];
                in->scale = 1 << (sib >> 6);
                in->index = (sib >> 3) & 7;
                if (in->index == 4)
                    in->index = -1;
                in->base = sib & 7;
                if (in->base == 5 && in->mod == 0)
                {
                    in->base = -1;
                    in->disp = Rd32(p + i);
                    i += 4;
                }
            }
            else if (in->rm == 5 && in->mod == 0)
            {
                in->disp = Rd32(p + i);
                i += 4;
            }
            else
                in->base = in->rm;
            if (in->mod == 1)
                in->disp = (int8_t)p[i++];
            else if (in->mod == 2)
            {
                in->disp = Rd32(p + i);
                i += 4;
            }
        }
    }
    in->len = i;
}

/* The ModRM reg field names a general register that the instruction reads
   (so moving it would change the operand, not only the address). */
static int RegFieldIsSource(const struct NpInsn *in)
{
    uint8_t op = in->op;

    if (!in->hasModrm)
        return 0;
    if (!in->twoByte)
    {
        if (op < 0x40)
            return (op & 7) < 4; /* ALU r/m,r and r,r/m (cmp, add, ...) */
        switch (op)
        {
        case 0x84: case 0x85: case 0x86: case 0x87: /* test, xchg */
        case 0x62: case 0x63:
            return 1;
        }
        return 0; /* mov (88-8B), lea, imul r,r/m,imm (dest only), and /digit forms */
    }
    if (in->threeByte)
        return 0;
    switch (op)
    {
    case 0xA3: case 0xA4: case 0xA5: case 0xAB: case 0xAC: case 0xAD: case 0xAF:
    case 0xB0: case 0xB1: case 0xB3: case 0xBB: case 0xC0: case 0xC1:
        return 1;
    }
    return 0; /* movzx/movsx, cmovcc, bsf/bsr (dest only), SSE/x87 (not a GPR) */
}

/* mul/imul/div/idiv r/m (F6/F7 /4-/7) also use eax and edx. */
static int ImplicitEaxEdx(const struct NpInsn *in)
{
    return !in->twoByte && (in->op == 0xF6 || in->op == 0xF7) && in->reg >= 4;
}

static uint32_t LatchBytes(uint32_t latch, uint32_t addr, int width)
{
    uint32_t v = 0;
    int i;

    for (i = 0; i < width; i++)
        v |= ((latch >> (8 * ((addr + i) & 3))) & 0xFFu) << (8 * i);
    return v;
}

static void SetReg(greg_t *g, int reg, int width, uint32_t v)
{
    if (width == 1)
    {
        int r = reg & 3, shift = reg >= 4 ? 8 : 0;
        g[kGreg[r]] = (greg_t)(((uint32_t)g[kGreg[r]] & ~(0xFFu << shift)) | ((v & 0xFFu) << shift));
    }
    else if (width == 2)
        g[kGreg[reg]] = (greg_t)(((uint32_t)g[kGreg[reg]] & 0xFFFF0000u) | (v & 0xFFFFu));
    else
        g[kGreg[reg]] = (greg_t)v;
}

/* Absolute address (no register): plain loads are emulated, plain stores
   skipped. Returns the instruction length, or 0 when not handled. */
static int EmulateAbsolute(greg_t *g, const struct NpInsn *in, uint32_t ea, uint32_t latch)
{
    int w4 = in->op16 ? 2 : 4;

    if (in->addr16 || in->segFsGs)
        return 0;
    if (!in->twoByte && !in->hasModrm)
    {
        switch (in->op)
        {
        case 0xA0: SetReg(g, 0, 1, LatchBytes(latch, ea, 1)); return in->pre + 5;
        case 0xA1: SetReg(g, 0, w4, LatchBytes(latch, ea, w4)); return in->pre + 5;
        case 0xA2: case 0xA3: return in->pre + 5;
        }
        return 0;
    }
    if (!in->hasModrm || in->mod == 3)
        return 0;
    if (!in->twoByte)
    {
        switch (in->op)
        {
        case 0x8A: SetReg(g, in->reg, 1, LatchBytes(latch, ea, 1)); return in->len;
        case 0x8B: SetReg(g, in->reg, w4, LatchBytes(latch, ea, w4)); return in->len;
        case 0x88: case 0x89: return in->len;
        case 0xC6: return in->reg == 0 ? in->len + 1 : 0;
        case 0xC7: return in->reg == 0 ? in->len + w4 : 0;
        }
        return 0;
    }
    if (in->threeByte)
        return 0;
    switch (in->op)
    {
    case 0xB6: SetReg(g, in->reg, w4, LatchBytes(latch, ea, 1)); return in->len;
    case 0xB7: SetReg(g, in->reg, w4, LatchBytes(latch, ea, 2)); return in->len;
    case 0xBE: SetReg(g, in->reg, w4, (uint32_t)(int32_t)(int8_t)LatchBytes(latch, ea, 1)); return in->len;
    case 0xBF: SetReg(g, in->reg, w4, (uint32_t)(int32_t)(int16_t)LatchBytes(latch, ea, 2)); return in->len;
    }
    return 0;
}

/* Fill the shadow pages around GBA address ea with the latch word. */
static void FillShadow(uint32_t ea, uint32_t latch)
{
    uint32_t lo = sShadow - NP_SHADOW_GUARD, hi = sShadow + NP_SHADOW_SPAN;
    uint32_t page = ((sShadow + ea) & ~0xFFFu) - 0x1000u;
    int k;

    for (k = 0; k < 3; k++, page += 0x1000u)
    {
        uint32_t *w = (uint32_t *)(uintptr_t)page;
        unsigned i;

        if (page < lo || page + 0x1000u > hi)
            continue;
        for (i = 0; i < 0x400; i++)
            w[i] = latch;
    }
}

/* ------------------------------------------------------------------------ */
/* Site table                                                                */

/* The callers of the faulting instruction (return addresses), from the
   unwinder; it reports the interrupted instruction itself as one frame. */
static int Callers(uint32_t eip, uint32_t eipAfter, uint32_t *out)
{
    void *fr[32];
    int n = backtrace(fr, 32), i, k = -1, m = 0;

    for (i = 0; i < n; i++)
    {
        uint32_t a = (uint32_t)(uintptr_t)fr[i];
        if (a == eip || a == eipAfter)
        {
            k = i;
            break;
        }
    }
    if (k < 0)
        return 0;
    for (i = k + 1; i < n && m < NP_CALLERS; i++)
        out[m++] = (uint32_t)(uintptr_t)fr[i];
    return m;
}

static void Out(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void Out(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fflush(stdout);
}

static struct NpSite *Record(uint32_t eip, uint32_t eipAfter, uint32_t addr, int write, int how, int reg,
    int rule, uint32_t latch, int aliased, const char *note)
{
    uint32_t callers[NP_CALLERS] = { 0 };
    int nc = Callers(eip, eipAfter, callers);
    uint32_t tick = Tick();
    struct NpSite *s = NULL;
    unsigned i;

    if ((++sHits & 0xFFFFFu) == 0) /* a loop that keeps faulting shows up in the log */
        Out("[nullprobe] %llu hits so far, tick %u, frame %u, last EIP 0x%08X\n", (unsigned long long)sHits,
            tick, gPortFrameCount, eip);
    for (i = 0; i < sSiteCount; i++)
    {
        if (sSites[i].eip == eip && sSites[i].callers[0] == callers[0] && sSites[i].how == how)
        {
            s = &sSites[i];
            break;
        }
    }
    if (s == NULL)
    {
        if (sSiteCount == NP_MAX_SITES)
        {
            sSitesLost++;
            return NULL;
        }
        s = &sSites[sSiteCount++];
        memset(s, 0, sizeof(*s));
        s->eip = eip;
        memcpy(s->callers, callers, sizeof(callers));
        s->nCallers = (uint8_t)nc;
        s->addrFirst = s->addrMin = s->addrMax = addr;
        s->firstTick = tick;
        s->firstFrame = gPortFrameCount;
        s->firstOnMain = gPortOnMainCalls;
        s->latchFirst = latch;
        s->ruleFirst = (uint8_t)rule;
        s->how = (uint8_t)how;
        s->reg = (uint8_t)reg;
        if (eip >= 0x10000000u && eip < 0x20000000u)
            memcpy(s->insn, (const void *)(uintptr_t)eip, sizeof(s->insn));
        snprintf(s->note, sizeof(s->note), "%s", note ? note : "");
        Out("[nullprobe] new site %u: %s %s 0x%08X at EIP 0x%08X (caller 0x%08X), tick %u, frame %u, "
            "latch %08X (%s)%s%s\n", sSiteCount - 1, kHowName[how], write ? "write" : "read", addr, eip,
            callers[0], tick, gPortFrameCount, latch, kRuleName[rule], note ? ", " : "", note ? note : "");
    }
    s->hits++;
    s->ruleHits[rule]++;
    if (write)
        s->writeHits++;
    else
        s->readHits++;
    if (aliased)
        s->aliasedHits++;
    if (addr < s->addrMin)
        s->addrMin = addr;
    if (addr > s->addrMax)
        s->addrMax = addr;
    s->lastTick = tick;
    s->lastFrame = gPortFrameCount;
    return s;
}

static void DumpTo(FILE *f, const char *why)
{
    unsigned i, j;

    fprintf(f, "{\"nullprobe\":1,\"route\":\"%s\",\"keys\":\"%s\",\"end\":\"%s\",\"tick\":%u,\"frame\":%u,"
               "\"onmain\":%u,\"tick_source\":\"%s\",\"sites\":%u,\"sites_lost\":%u,\"hits\":%llu,"
               "\"shadow\":\"0x%08X\"}\n",
        sRoute, sKeys ? sKeys : "", why, Tick(), gPortFrameCount, gPortOnMainCalls,
        PortGate0Tick != NULL ? "gate0" : "frame", sSiteCount, sSitesLost, (unsigned long long)sHits, sShadow);
    for (i = 0; i < sSiteCount; i++)
    {
        const struct NpSite *s = &sSites[i];

        fprintf(f, "{\"site\":%u,\"route\":\"%s\",\"eip\":\"0x%08X\",\"callers\":[", i, sRoute, s->eip);
        for (j = 0; j < s->nCallers; j++)
            fprintf(f, "%s\"0x%08X\"", j ? "," : "", s->callers[j]);
        fprintf(f, "],\"how\":\"%s\",\"reg\":\"%s\",\"reads\":%u,\"writes\":%u,\"addr_first\":\"0x%08X\","
                   "\"addr_min\":\"0x%08X\",\"addr_max\":\"0x%08X\",\"first_tick\":%u,\"last_tick\":%u,"
                   "\"first_frame\":%u,\"last_frame\":%u,\"first_onmain\":%u,\"hits\":%u,"
                   "\"latch_first\":\"%08X\",\"rule_first\":\"%s\",\"rule_hits\":{",
            kHowName[s->how], (s->how == HOW_RELOCATE || s->how == HOW_MIRROR) ? kRegName[s->reg] : "",
            s->readHits, s->writeHits,
            s->addrFirst, s->addrMin, s->addrMax, s->firstTick, s->lastTick, s->firstFrame, s->lastFrame,
            s->firstOnMain, s->hits, s->latchFirst, kRuleName[s->ruleFirst]);
        for (j = 0; j < RULE_COUNT; j++)
            fprintf(f, "%s\"%s\":%u", j ? "," : "", kRuleName[j], s->ruleHits[j]);
        fprintf(f, "},\"aliased_hits\":%u,\"insn\":\"", s->aliasedHits);
        for (j = 0; j < sizeof(s->insn); j++)
            fprintf(f, "%02x", s->insn[j]);
        fprintf(f, "\",\"note\":\"%s\"}\n", s->note);
    }
}

void PortNullProbe_Dump(const char *why)
{
    FILE *f;

    if (!sOn)
        return;
    Out("[nullprobe] %s: %u sites, %llu hits, tick %u, frame %u\n", why, sSiteCount,
        (unsigned long long)sHits, Tick(), gPortFrameCount);
    if (sOutPath == NULL)
        return;
    f = fopen(sOutPath, "w");
    if (f == NULL)
    {
        Out("[nullprobe] cannot write %s\n", sOutPath);
        return;
    }
    DumpTo(f, why);
    fclose(f);
}

static void AtExitDump(void)
{
    PortNullProbe_Dump("exit");
}

static void OnStopSignal(int sig)
{
    char why[32];

    snprintf(why, sizeof(why), "signal %d", sig);
    PortNullProbe_Dump(why);
    signal(sig, SIG_DFL);
    raise(sig);
}

/* ------------------------------------------------------------------------ */
/* Fault and trap                                                            */

static void FinishPending(greg_t *g)
{
    int i;

    for (i = 0; i < sPend.n; i++)
    {
        uint32_t v = (uint32_t)g[sPend.greg[i]] - sPend.delta[i];
        uint32_t d = v - sPend.orig[i];

        /* Still the moved pointer (string ops step it a little): move it back.
           Otherwise the instruction wrote the register (a load into it). */
        if (d <= 64 || d >= 0u - 64)
            g[sPend.greg[i]] = (greg_t)v;
    }
    sPend.active = 0;
    sPend.n = 0;
}

static void OnTrap(int sig, siginfo_t *info, void *ctx)
{
    ucontext_t *uc = (ucontext_t *)ctx;
    greg_t *g = uc->uc_mcontext.gregs;

    (void)sig;
    (void)info;
    if (!sPend.active)
    {
        Out("[nullprobe] unexpected SIGTRAP at EIP 0x%08X\n", (uint32_t)g[REG_EIP]);
        PortNullProbe_Dump("unexpected SIGTRAP");
        _exit(5);
    }
    FinishPending(g);
    g[REG_EFL] &= ~(greg_t)NP_EFLAGS_TF;
}

static int Unsupported(uint32_t eip, uint32_t addr, int write, int rule, uint32_t latch, const char *why)
{
    Record(eip, eip, addr, write, HOW_UNSUPPORTED, 0, rule, latch, 0, why);
    Out("[nullprobe] not handled (%s): %s 0x%08X at EIP 0x%08X; the fault stays fatal\n", why,
        write ? "write" : "read", addr, eip);
    return 0;
}

/* GBA memory mirrors that the host does not map (GBATEK "Memory Map"): the GBA
   address the access really reaches. 0 when addr is not in one. */
static int MirrorTarget(uint32_t a, uint32_t *t)
{
    if (a >= 0x02040000u && a < 0x03000000u)
        *t = 0x02000000u | (a & 0x3FFFFu); /* EWRAM, 256 KiB */
    else if (a >= 0x03008000u && a < 0x04000000u)
        *t = 0x03000000u | (a & 0x7FFFu); /* IWRAM, 32 KiB */
    else if (a >= 0x05001000u && a < 0x06000000u)
        *t = 0x05000000u | (a & 0x3FFu); /* palette RAM, 1 KiB */
    else if (a >= 0x06018000u && a < 0x07000000u)
    {
        uint32_t o = a & 0x1FFFFu; /* VRAM, 96 KiB in 128 KiB steps */
        *t = 0x06000000u | (o >= 0x18000u ? o - 0x8000u : o);
    }
    else if (a >= 0x07001000u && a < 0x08000000u)
        *t = 0x07000000u | (a & 0x3FFu); /* OAM, 1 KiB */
    else if (a >= 0x0A000000u && a < 0x0E000000u)
        *t = 0x08000000u | (a & 0x01FFFFFFu); /* ROM wait-state mirrors */
    else if (a >= 0x0E010000u && a < 0x10000000u)
        *t = 0x0E000000u | (a & 0xFFFFu); /* SRAM, 64 KiB */
    else
        return 0;
    return 1;
}

/* Unused GBA address space that faults on the host, outside the low range:
   I/O above the host's 4 KiB, and 0x10000000 and up (the host maps its own
   code and data there, so only what faults is unused on both). The GBA reads
   open bus there (the prefetched opcode, not the BIOS latch). */
static int IsUnusedHigh(uint32_t a)
{
    return (a >= 0x04001000u && a < 0x05000000u) || a >= 0x10000000u;
}

/* The shadow page that stands in for an unused high address: same page
   offset, so the latch lanes follow the address. */
#define NP_HIGH_PAGE (NP_SHADOW_SPAN - 0x2000u)

int PortNullProbe_OnFault(void *ctx, uint32_t addr)
{
    ucontext_t *uc = (ucontext_t *)ctx;
    greg_t *g;
    uint32_t eip, latch, ea = 0, mirror = 0;
    int write, rule, r = -1, aliased = 0, low, high = 0;
    struct NpInsn in;

    if (!sOn)
        return 0;
    low = IsLow(addr);
    if (!low && !MirrorTarget(addr, &mirror))
    {
        if (!IsUnusedHigh(addr))
            return 0;
        high = 1;
    }
    g = uc->uc_mcontext.gregs;
    eip = (uint32_t)g[REG_EIP];
    write = ((uint32_t)g[REG_ERR] & NP_ERR_WRITE) != 0;
    if (low || high)
        rule = LatchRule(high ? NP_BIOS_END : addr, &latch); /* high: always undecided (not BIOS) */
    else
        rule = RULE_MIRROR, latch = mirror;

    if (((uint32_t)g[REG_ERR] & NP_ERR_FETCH) || eip == addr)
        return Unsupported(eip, addr, 0, rule, latch, "jump to an unmapped address");
    if (sPend.active && sPend.eip != eip)
        FinishPending(g); /* not expected: the trap always comes first */
    if (sPend.active && sPend.n == 2)
        return Unsupported(eip, addr, write, rule, latch, "third faulting operand");

    Decode((const uint8_t *)(uintptr_t)eip, &in);
    if (in.addr16 || in.segFsGs)
        return Unsupported(eip, addr, write, rule, latch, "addr16 or fs/gs prefix");

    if (!in.twoByte && !in.hasModrm && ((in.op >= 0xA4 && in.op <= 0xA7) || (in.op >= 0xAA && in.op <= 0xAF)))
    {
        /* String op: esi (movs, cmps, lods) or edi (movs, cmps, stos, scas). */
        uint32_t esi = (uint32_t)g[REG_ESI], edi = (uint32_t)g[REG_EDI];
        int usesEsi = in.op <= 0xA7 || in.op == 0xAC || in.op == 0xAD;
        int usesEdi = in.op != 0xAC && in.op != 0xAD;
        uint32_t dEsi = addr - esi, dEdi = addr - edi;
        int nearEsi = usesEsi && (dEsi < 16 || dEsi >= 0u - 16);
        int nearEdi = usesEdi && (dEdi < 16 || dEdi >= 0u - 16);

        if (nearEsi && (!nearEdi || !write))
            r = 6, ea = esi;
        else if (nearEdi)
            r = 7, ea = edi;
        else
            return Unsupported(eip, addr, write, rule, latch, "string op, no register at the address");
    }
    else if (in.hasModrm && in.mod != 3)
    {
        uint32_t base = in.base >= 0 ? (uint32_t)g[kGreg[in.base]] : 0;
        uint32_t index = in.index >= 0 ? (uint32_t)g[kGreg[in.index]] : 0;

        ea = base + index * (uint32_t)in.scale + (uint32_t)in.disp;
        if ((uint32_t)(addr - ea) >= 16)
            return Unsupported(eip, addr, write, rule, latch, "fault is not at the ModRM operand");
        if (in.base < 0 && in.index < 0)
        {
            int len = (low || high) ? EmulateAbsolute(g, &in, ea, latch) : 0;
            if (len == 0)
                return Unsupported(eip, addr, write, rule, latch, "absolute address, not a plain mov");
            Record(eip, eip, addr, write, HOW_EMULATE, 0, rule, latch, 0, NULL);
            g[REG_EIP] = (greg_t)(eip + (uint32_t)len);
            return 1;
        }
        if (in.base >= 0 && in.base == in.index)
            return Unsupported(eip, addr, write, rule, latch, "base == index");
        r = in.base >= 0 ? in.base : in.index;
        if (r == 4)
            return Unsupported(eip, addr, write, rule, latch, "esp-based address");
        aliased = (RegFieldIsSource(&in) && in.reg == r) || (ImplicitEaxEdx(&in) && (r == 0 || r == 2));
    }
    else if (!in.twoByte && !in.hasModrm && in.op >= 0xA0 && in.op <= 0xA3)
    {
        int len;

        if (!low && !high)
            return Unsupported(eip, addr, write, rule, latch, "absolute address in a mirror");
        ea = (uint32_t)Rd32((const uint8_t *)(uintptr_t)eip + in.pre + 1);
        len = EmulateAbsolute(g, &in, ea, latch);
        Record(eip, eip, addr, write, HOW_EMULATE, 0, rule, latch, 0, NULL);
        g[REG_EIP] = (greg_t)(eip + (uint32_t)len);
        return 1;
    }
    else
        return Unsupported(eip, addr, write, rule, latch, "no memory operand found");

    {
        /* Low address: into the shadow at S + address. Unused high address:
           into the shadow page NP_HIGH_PAGE, same page offset. Mirror: onto
           the real GBA address. */
        uint32_t target = low ? sShadow + addr
                        : high ? sShadow + NP_HIGH_PAGE + (addr & 0xFFFu) : mirror;
        int32_t move = (int32_t)(target - addr);
        uint32_t delta = (uint32_t)((in.hasModrm && in.base < 0) ? move / in.scale : move);
        int i = sPend.active ? sPend.n : 0;
        const char *note = NULL;

        if (sPend.active && sPend.greg[0] == kGreg[r])
            return Unsupported(eip, addr, write, rule, latch, "same register faulted twice");
        if (low || high)
            FillShadow(target - sShadow, latch);
        if (aliased)
            note = "operand register is the pointer: result may differ";
        else if (high)
            note = "unused GBA area above 0x04000FFF: open bus";
        else if (!low)
            note = "GBA memory mirror";
        Record(eip, eip, addr, write, (low || high) ? HOW_RELOCATE : HOW_MIRROR, r, rule, latch, aliased, note);
        sPend.active = 1;
        sPend.eip = eip;
        sPend.greg[i] = kGreg[r];
        sPend.delta[i] = delta;
        sPend.orig[i] = (uint32_t)g[kGreg[r]];
        sPend.n = i + 1;
        g[kGreg[r]] = (greg_t)((uint32_t)g[kGreg[r]] + delta);
        g[REG_EFL] |= (greg_t)NP_EFLAGS_TF;
    }
    return 1;
}

void PortNullProbe_NoteDroppedWrite(uint32_t eip, uint32_t eipAfter, uint32_t addr)
{
    uint32_t latch;
    int rule;

    if (!sOn)
        return;
    rule = LatchRule(addr, &latch);
    Record(eip, eipAfter, addr, 1, HOW_H1, 0, rule, latch, 0, NULL);
}

/* An integer division trap (SIGFPE; see the header): the GBA libgcc result in
   eax (quotient) and edx (remainder), the instruction skipped. Recorded with
   the dividend as the address and the divisor as the latch word. 0: not
   handled, the fault stays fatal. */
int PortNullProbe_OnDivFault(void *ctx)
{
    ucontext_t *uc = (ucontext_t *)ctx;
    greg_t *g;
    uint32_t eip, eax, edx, divisor, q, r;
    int sign, how;
    struct NpInsn in;
    char note[64];

    if (!sOn)
        return 0;
    g = uc->uc_mcontext.gregs;
    eip = (uint32_t)g[REG_EIP];
    Decode((const uint8_t *)(uintptr_t)eip, &in);
    if (in.twoByte || in.op != 0xF7 || !in.hasModrm || (in.reg != 6 && in.reg != 7) || in.op16 || in.addr16 ||
        in.segFsGs)
        return Unsupported(eip, 0, 0, RULE_DIV, 0, "division trap, not a 32-bit div/idiv");
    sign = in.reg == 7;
    if (in.mod == 3)
        divisor = (uint32_t)g[kGreg[in.rm]];
    else
    {
        /* The operand was read before the divide trapped, so it is mapped
           (the shadow, when the probe moved its register there first). */
        uint32_t base = in.base >= 0 ? (uint32_t)g[kGreg[in.base]] : 0;
        uint32_t index = in.index >= 0 ? (uint32_t)g[kGreg[in.index]] : 0;

        divisor = *(const volatile uint32_t *)(uintptr_t)(base + index * (uint32_t)in.scale + (uint32_t)in.disp);
    }
    eax = (uint32_t)g[REG_EAX];
    edx = (uint32_t)g[REG_EDX];
    if (sign ? edx != (uint32_t)((int32_t)eax >> 31) : edx != 0)
        return Unsupported(eip, eax, 0, RULE_DIV, divisor, "division trap with a 64-bit dividend");
    if (divisor == 0)
    {
        q = r = 0; /* Ldiv0 in all four routines: bl __div0 (returns at once), r0 = 0 */
        how = HOW_DIV0;
    }
    else if (sign && eax == 0x80000000u && divisor == 0xFFFFFFFFu)
    {
        q = 0x80000000u; /* 0x80000000 / 1 as unsigned magnitudes, signs equal: not negated */
        r = 0;
        how = HOW_DIVOVF;
    }
    else
        return Unsupported(eip, eax, 0, RULE_DIV, divisor, "division trap, divisor not 0 or -1");
    if (sPend.active)
    {
        /* The divide's memory operand was moved into the shadow: move its
           register back now, since the trap after it will not come. */
        FinishPending(g);
        g[REG_EFL] &= ~(greg_t)NP_EFLAGS_TF;
    }
    snprintf(note, sizeof(note), "%s 0x%08X by 0x%08X: GBA q 0x%08X r 0x%08X", sign ? "idiv" : "div", eax,
        divisor, q, r);
    Record(eip, eip, eax, 0, how, 0, RULE_DIV, divisor, 0, note);
    g[REG_EAX] = (greg_t)q;
    g[REG_EDX] = (greg_t)r;
    g[REG_EIP] = (greg_t)(eip + (uint32_t)in.len);
    return 1;
}

int PortNullProbe_Enabled(void)
{
    return sOn;
}

int PortNullProbe_Init(void)
{
    static const uint32_t kCandidates[] = { 0x60000000u, 0x58000000u, 0x68000000u, 0x50000000u, 0x70000000u };
    const char *s = getenv("FE8PC_NULLPROBE");
    const char *route = getenv("FE8PC_NULLPROBE_ROUTE");
    struct sigaction sa;
    void *warm[4];
    unsigned i;

    if (s == NULL || s[0] == '\0' || strcmp(s, "0") == 0)
        return 0;
    for (i = 0; i < sizeof(kCandidates) / sizeof(kCandidates[0]) && sShadow == 0; i++)
    {
        void *want = (void *)(uintptr_t)(kCandidates[i] - NP_SHADOW_GUARD);
        void *got = mmap(want, NP_SHADOW_GUARD + NP_SHADOW_SPAN, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
        if (got == want)
            sShadow = kCandidates[i];
        else if (got != MAP_FAILED)
            munmap(got, NP_SHADOW_GUARD + NP_SHADOW_SPAN);
    }
    if (sShadow == 0)
    {
        PortLog("[nullprobe] cannot reserve a shadow area\n");
        return -1;
    }
    sOutPath = getenv("FE8PC_NULLPROBE_OUT");
    sKeys = getenv("GATE0_KEYS");
    if (route == NULL && sKeys != NULL)
    {
        const char *b = strrchr(sKeys, '/');
        route = b != NULL ? b + 1 : sKeys;
    }
    snprintf(sRoute, sizeof(sRoute), "%s", route != NULL ? route : "");
    if (strlen(sRoute) > 5 && strcmp(sRoute + strlen(sRoute) - 5, ".keys") == 0)
        sRoute[strlen(sRoute) - 5] = '\0';

    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = OnTrap;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGTRAP, &sa, NULL);
    /* The fault handler (main_pc.c) on its own stack, so a stack overflow in
       a run that the probe kept going is reported and dumped too. */
    {
        static uint8_t sAltStack[256 * 1024];
        stack_t ss;

        ss.ss_sp = sAltStack;
        ss.ss_size = sizeof(sAltStack);
        ss.ss_flags = 0;
        if (sigaltstack(&ss, NULL) == 0 && sigaction(SIGSEGV, NULL, &sa) == 0)
        {
            sa.sa_flags |= SA_ONSTACK;
            sigaction(SIGSEGV, &sa, NULL);
        }
    }
    signal(SIGTERM, OnStopSignal);
    signal(SIGINT, OnStopSignal);
    backtrace(warm, 4); /* loads the unwinder now, outside a signal handler */
    atexit(AtExitDump);
    sOn = 1;
    PortLog("[nullprobe] on: BIOS-area/NULL-page reads return the latch, integer division traps get the GBA "
            "libgcc result, and both are recorded; shadow 0x%08X, route '%s', table %s\n", sShadow, sRoute,
            sOutPath ? sOutPath : "(stdout summary only)");
    return 1;
}

#else /* not Linux i386: the probe is not available */

int PortNullProbe_Init(void)
{
    const char *s = getenv("FE8PC_NULLPROBE");

    if (s == NULL || s[0] == '\0' || strcmp(s, "0") == 0)
        return 0;
    PortLog("[nullprobe] FE8PC_NULLPROBE is only supported by the Linux i386 build\n");
    return -1;
}

int PortNullProbe_Enabled(void)
{
    return 0;
}

int PortNullProbe_OnFault(void *ctx, uint32_t addr)
{
    (void)ctx;
    (void)addr;
    return 0;
}

int PortNullProbe_OnDivFault(void *ctx)
{
    (void)ctx;
    return 0;
}

void PortNullProbe_NoteDroppedWrite(uint32_t eip, uint32_t eipAfter, uint32_t addr)
{
    (void)eip;
    (void)eipAfter;
    (void)addr;
}

void PortNullProbe_Dump(const char *why)
{
    (void)why;
}

#endif
