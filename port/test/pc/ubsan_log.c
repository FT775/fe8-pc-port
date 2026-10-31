/*
 * Bug sweep (a), UBSan (port/test/README.md, "Bug sweeps"; port/test/sweep.mk):
 * the run-time half of GCC's -fsanitize=undefined checks, linked only into the
 * UBSan sweep binary build/pc-gate0-ubsan/fe8pc, a Gate 0 test binary that is
 * never shipped. Its game and port code are compiled with
 * -fsanitize=undefined,bounds -fsanitize-recover=all (port/test/pc/
 * sweep_flags.mk); a check that fails calls one of the __ubsan_handle_*
 * functions below. This file itself is compiled without the checks.
 *
 * GCC's own runtime (libubsan) is not used: it reports each site once and keeps
 * no count, and it marks a site as reported by writing into the check's static
 * data, which lies in the game objects' .data (the save states refuse a run
 * that changed .data bytes that no symbol covers). Here:
 *   - Every check recovers: the handler records the hit and returns, and the
 *     code goes on with what the operation gave (the wrapped sum, the x86
 *     shift, the load through the misaligned pointer). A NULL load still
 *     faults after its report, as in every PC build. Only
 *     __builtin_unreachable cannot return (GCC emits nothing after it): it is
 *     recorded, then the run stops with a "[fault]" line (exit 6).
 *   - One record per site (the check's static data, one per check that GCC
 *     emitted, and the kind of failure, as one shift's data serves both its
 *     exponent and its base check): the check, file:line:column, the hit
 *     count, the first and last
 *     Gate 0 tick (PortGate0Tick, the hook's tick in progress), the message of
 *     the first hit with its values (worded as libubsan words them), and the
 *     return addresses of that first hit (backtrace()), named Function+offset
 *     from this binary's own symbol table.
 *   - A site's first hit is logged at once on stdout (the run's log), as one
 *     "[ubsan] " line.
 *   - FE8PC_UBSAN_OUT=PATH.jsonl: the table, one JSON line per site after a
 *     header line, sorted by file, line and column, written at exit and again
 *     at most once every 1,000 ticks while sites are hit (a run that faults
 *     leaves with _exit, port/hw/main_pc.c, so its table is the last one
 *     written; the [ubsan] lines name every site). PATH.txt beside it: the
 *     same table, one line per site. port/test/tools/ubsan_collate.py merges
 *     tables and adds file:line to each return address (addr2line).
 */
#define _GNU_SOURCE /* dladdr */
#include <dlfcn.h>
#include <elf.h>
#include <execinfo.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "port_hw.h"

/* GCC's check data (gcc/ubsan.cc; the same layout as compiler-rt's
   ubsan_handlers.h). */
struct SourceLocation
{
    const char *file;
    uint32_t line;
    uint32_t column;
};

struct TypeDescriptor
{
    uint16_t kind; /* 0 integer, 1 float, 0xFFFF other */
    uint16_t info; /* integer: log2(bit width) << 1 | signed; float: bit width */
    char name[1];
};

typedef uintptr_t ValueHandle;

struct TypeMismatchData
{
    struct SourceLocation loc;
    const struct TypeDescriptor *type;
    uint8_t logAlignment;
    uint8_t typeCheckKind;
};

struct OverflowData
{
    struct SourceLocation loc;
    const struct TypeDescriptor *type;
};

struct ShiftOutOfBoundsData
{
    struct SourceLocation loc;
    const struct TypeDescriptor *lhsType;
    const struct TypeDescriptor *rhsType;
};

struct OutOfBoundsData
{
    struct SourceLocation loc;
    const struct TypeDescriptor *arrayType;
    const struct TypeDescriptor *indexType;
};

struct UnreachableData
{
    struct SourceLocation loc;
};

struct VLABoundData
{
    struct SourceLocation loc;
    const struct TypeDescriptor *type;
};

struct InvalidValueData
{
    struct SourceLocation loc;
    const struct TypeDescriptor *type;
};

struct NonNullArgData
{
    struct SourceLocation loc;
    struct SourceLocation attrLoc;
    int argIndex;
};

struct NonNullReturnData
{
    struct SourceLocation attrLoc;
};

struct PointerOverflowData
{
    struct SourceLocation loc;
};

struct InvalidBuiltinData
{
    struct SourceLocation loc;
    uint8_t kind; /* 0 ctz, 1 clz */
};

struct FloatCastOverflowData
{
    struct SourceLocation loc;
    const struct TypeDescriptor *fromType;
    const struct TypeDescriptor *toType;
};

/* The hook's tick in progress (port/test/pc/gate0_hook.c); weak, as in
   port/hw/nullprobe.c. */
uint32_t PortGate0Tick(void) __attribute__((weak));

#define SITE_CAP 4096 /* a power of two */
#define SITE_BT  10
#define SITE_MSG 200
#define DUMP_EVERY 1000

struct Site
{
    const void *key;
    const char *check;
    struct SourceLocation loc;
    uint32_t id, count, firstTick, lastTick;
    void *bt[SITE_BT];
    int nbt;
    char msg[SITE_MSG];
};

static struct Site sSites[SITE_CAP];
static unsigned sSiteCount, sDroppedHits;
static uint32_t sHits;
static char sOutPath[1024];
static uint32_t sNextDumpTick;
static int sStarted, sDumping;

/* ---- this binary's function symbols, for Function+offset ---- */

struct FuncSym
{
    uint32_t addr, size;
    const char *name;
};

static struct FuncSym *sFuncs;
static unsigned sFuncCount;
static char *sStrtab;
static int sFuncsTried;
static uintptr_t sTextLo, sTextHi; /* the binary's executable segments (no PIE: link addresses) */

static int CmpFunc(const void *a, const void *b)
{
    const struct FuncSym *x = a, *y = b;

    return x->addr < y->addr ? -1 : x->addr > y->addr;
}

static void LoadFuncs(void)
{
    FILE *f;
    Elf32_Ehdr eh;
    Elf32_Shdr *sh = NULL;
    Elf32_Sym *syms = NULL;
    unsigned i, n = 0;

    sFuncsTried = 1;
    f = fopen("/proc/self/exe", "rb");
    if (f == NULL)
        return;
    if (fread(&eh, sizeof(eh), 1, f) != 1 || memcmp(eh.e_ident, ELFMAG, SELFMAG) != 0
        || eh.e_ident[EI_CLASS] != ELFCLASS32 || eh.e_shentsize != sizeof(Elf32_Shdr))
        goto done;
    for (i = 0; i < eh.e_phnum && eh.e_phentsize == sizeof(Elf32_Phdr); i++)
    {
        Elf32_Phdr ph;

        if (fseek(f, (long)(eh.e_phoff + i * sizeof(ph)), SEEK_SET) != 0 || fread(&ph, sizeof(ph), 1, f) != 1)
            break;
        if (ph.p_type == PT_LOAD && (ph.p_flags & PF_X))
        {
            if (sTextLo == 0 || ph.p_vaddr < sTextLo)
                sTextLo = ph.p_vaddr;
            if (ph.p_vaddr + ph.p_memsz > sTextHi)
                sTextHi = ph.p_vaddr + ph.p_memsz;
        }
    }
    sh = calloc(eh.e_shnum, sizeof(*sh));
    if (sh == NULL || fseek(f, (long)eh.e_shoff, SEEK_SET) != 0 || fread(sh, sizeof(*sh), eh.e_shnum, f) != eh.e_shnum)
        goto done;
    for (i = 0; i < eh.e_shnum; i++)
    {
        const Elf32_Shdr *s = &sh[i], *str;

        if (s->sh_type != SHT_SYMTAB || s->sh_link >= eh.e_shnum)
            continue;
        str = &sh[s->sh_link];
        n = s->sh_size / sizeof(Elf32_Sym);
        syms = malloc(s->sh_size);
        sStrtab = malloc(str->sh_size + 1);
        if (syms == NULL || sStrtab == NULL || fseek(f, (long)s->sh_offset, SEEK_SET) != 0
            || fread(syms, sizeof(Elf32_Sym), n, f) != n || fseek(f, (long)str->sh_offset, SEEK_SET) != 0
            || fread(sStrtab, 1, str->sh_size, f) != str->sh_size)
        {
            n = 0;
            break;
        }
        sStrtab[str->sh_size] = '\0';
        break;
    }
    if (n != 0 && (sFuncs = calloc(n, sizeof(*sFuncs))) != NULL)
    {
        for (i = 0; i < n; i++)
            if (ELF32_ST_TYPE(syms[i].st_info) == STT_FUNC && syms[i].st_value != 0)
            {
                sFuncs[sFuncCount].addr = syms[i].st_value;
                sFuncs[sFuncCount].size = syms[i].st_size;
                sFuncs[sFuncCount].name = sStrtab + syms[i].st_name;
                sFuncCount++;
            }
        qsort(sFuncs, sFuncCount, sizeof(*sFuncs), CmpFunc);
    }
done:
    free(syms);
    free(sh);
    fclose(f);
}

/* "Function+0xOFF" for a code address of the binary (a return address may be
   one past the function's last byte: a call at its end), "library+0xOFF" for
   one in a shared library (the C library's startup code). */
static void NameAddr(uintptr_t a, char *buf, size_t n)
{
    unsigned lo = 0, hi;

    if (!sFuncsTried)
        LoadFuncs();
    if (a < sTextLo || a >= sTextHi)
    {
        Dl_info info;

        if (dladdr((void *)a, &info) != 0 && info.dli_fname != NULL)
        {
            const char *base = strrchr(info.dli_fname, '/');

            snprintf(buf, n, "%s+0x%x", base ? base + 1 : info.dli_fname, (unsigned)(a - (uintptr_t)info.dli_fbase));
        }
        else
            snprintf(buf, n, "0x%08x", (unsigned)a);
        return;
    }
    hi = sFuncCount;
    while (lo < hi)
    {
        unsigned mid = (lo + hi) / 2;

        if (sFuncs[mid].addr <= a)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo > 0 && a <= sFuncs[lo - 1].addr + (sFuncs[lo - 1].size != 0 ? sFuncs[lo - 1].size : 0x40))
        snprintf(buf, n, "%s+0x%x", sFuncs[lo - 1].name, (unsigned)(a - sFuncs[lo - 1].addr));
    else
        snprintf(buf, n, "0x%08x", (unsigned)a);
}

/* ---- values, as compiler-rt's Value class reads them ---- */

/* GCC's type names carry their quotes ("'int'"). */
static const char *TypeName(const struct TypeDescriptor *t)
{
    return t != NULL ? t->name : "'?'";
}

static unsigned IntWidth(const struct TypeDescriptor *t)
{
    return 1u << (t->info >> 1);
}

static int IsSignedInt(const struct TypeDescriptor *t)
{
    return t != NULL && t->kind == 0 && (t->info & 1);
}

/* Integers no wider than a pointer are passed in the handle, wider ones
   through a pointer to them. */
static int64_t SIntValue(const struct TypeDescriptor *t, ValueHandle v)
{
    unsigned w = IntWidth(t);

    if (w <= 32)
    {
        unsigned extra = 64 - w;

        return (int64_t)((uint64_t)v << extra) >> extra;
    }
    if (w == 64)
        return *(const int64_t *)v;
    return 0;
}

static uint64_t UIntValue(const struct TypeDescriptor *t, ValueHandle v)
{
    unsigned w = IntWidth(t);

    if (w <= 32)
        return (uint64_t)v;
    if (w == 64)
        return *(const uint64_t *)v;
    return 0;
}

static int IsNegative(const struct TypeDescriptor *t, ValueHandle v)
{
    return IsSignedInt(t) && SIntValue(t, v) < 0;
}

static long double FloatValue(const struct TypeDescriptor *t, ValueHandle v)
{
    if (t->info == 32)
    {
        float x;
        uint32_t bits = (uint32_t)v;

        memcpy(&x, &bits, sizeof(x));
        return x;
    }
    if (t->info == 64)
        return *(const double *)v;
    return *(const long double *)v;
}

static void FormatValue(char *buf, size_t n, const struct TypeDescriptor *t, ValueHandle v)
{
    if (t == NULL)
        snprintf(buf, n, "0x%x", (unsigned)v);
    else if (t->kind == 0 && IntWidth(t) > 64)
        snprintf(buf, n, "<%u-bit integer>", IntWidth(t));
    else if (t->kind == 0 && (t->info & 1))
        snprintf(buf, n, "%lld", (long long)SIntValue(t, v));
    else if (t->kind == 0)
        snprintf(buf, n, "%llu", (unsigned long long)UIntValue(t, v));
    else if (t->kind == 1)
        snprintf(buf, n, "%Lg", FloatValue(t, v));
    else
        snprintf(buf, n, "<value of type %s>", t->name);
}

/* ---- the table ---- */

static int CmpSite(const void *a, const void *b)
{
    const struct Site *x = *(const struct Site *const *)a, *y = *(const struct Site *const *)b;
    int c = strcmp(x->loc.file ? x->loc.file : "", y->loc.file ? y->loc.file : "");

    if (c != 0)
        return c;
    if (x->loc.line != y->loc.line)
        return x->loc.line < y->loc.line ? -1 : 1;
    if (x->loc.column != y->loc.column)
        return x->loc.column < y->loc.column ? -1 : 1;
    return strcmp(x->check, y->check);
}

static void JsonString(FILE *f, const char *s)
{
    fputc('"', f);
    for (; s != NULL && *s != '\0'; s++)
    {
        unsigned char c = (unsigned char)*s;

        if (c == '"' || c == '\\')
            fprintf(f, "\\%c", c);
        else if (c < 0x20)
            fprintf(f, "\\u%04x", c);
        else
            fputc(c, f);
    }
    fputc('"', f);
}

static uint32_t CurrentTick(void)
{
    return PortGate0Tick != NULL ? PortGate0Tick() : 0;
}

static void Dump(const char *why)
{
    static struct Site *order[SITE_CAP];
    char tmp[1100], txt[1100];
    unsigned i, k, n = 0;
    FILE *f, *t;
    size_t len;

    if (sOutPath[0] == '\0' || sDumping)
        return;
    sDumping = 1;
    for (i = 0; i < SITE_CAP; i++)
        if (sSites[i].key != NULL)
            order[n++] = &sSites[i];
    qsort(order, n, sizeof(order[0]), CmpSite);
    snprintf(tmp, sizeof(tmp), "%s.tmp", sOutPath);
    len = strlen(sOutPath);
    if (len > 6 && strcmp(sOutPath + len - 6, ".jsonl") == 0)
        snprintf(txt, sizeof(txt), "%.*s.txt", (int)(len - 6), sOutPath);
    else
        snprintf(txt, sizeof(txt), "%s.txt", sOutPath);
    f = fopen(tmp, "w");
    t = fopen(txt, "w");
    if (f != NULL)
    {
        char exe[512];
        ssize_t m = readlink("/proc/self/exe", exe, sizeof(exe) - 1);

        exe[m > 0 ? m : 0] = '\0';
        fprintf(f, "{\"ubsan\":1,\"exe\":");
        JsonString(f, exe);
        fprintf(f, ",\"end\":\"%s\",\"tick\":%u,\"sites\":%u,\"hits\":%u,\"hits_lost\":%u}\n", why, CurrentTick(), n,
            sHits, sDroppedHits);
    }
    if (t != NULL)
        fprintf(t, "# UBSan sweep: %u sites, %u hits (%s at tick %u; hits at sites not kept, the table being full: %u)\n"
                   "#     hits  first-last tick  file:line:col  check  message  | first hit: where < callers\n",
            n, sHits, why, CurrentTick(), sDroppedHits);
    for (i = 0; i < n; i++)
    {
        const struct Site *s = order[i];
        char name[160];

        if (f != NULL)
        {
            fprintf(f, "{\"site\":%u,\"check\":\"%s\",\"file\":", s->id, s->check);
            JsonString(f, s->loc.file);
            fprintf(f, ",\"line\":%u,\"col\":%u,\"count\":%u,\"first_tick\":%u,\"last_tick\":%u,\"msg\":",
                s->loc.line, s->loc.column, s->count, s->firstTick, s->lastTick);
            JsonString(f, s->msg);
            fprintf(f, ",\"bt\":[");
            for (k = 0; k < (unsigned)s->nbt; k++)
                fprintf(f, "%s\"0x%08x\"", k ? "," : "", (unsigned)(uintptr_t)s->bt[k]);
            fprintf(f, "],\"where\":[");
            for (k = 0; k < (unsigned)s->nbt; k++)
            {
                NameAddr((uintptr_t)s->bt[k], name, sizeof(name));
                if (k)
                    fputc(',', f);
                JsonString(f, name);
            }
            fprintf(f, "]}\n");
        }
        if (t != NULL)
        {
            fprintf(t, "%10u  %u-%u  %s:%u:%u  %s  %s  |", s->count, s->firstTick, s->lastTick,
                s->loc.file ? s->loc.file : "?", s->loc.line, s->loc.column, s->check, s->msg);
            for (k = 0; k < (unsigned)s->nbt && k < 6; k++)
            {
                NameAddr((uintptr_t)s->bt[k], name, sizeof(name));
                fprintf(t, "%s%s", k ? " < " : " ", name);
            }
            fputc('\n', t);
        }
    }
    if (f != NULL)
    {
        fclose(f);
        rename(tmp, sOutPath);
    }
    if (t != NULL)
        fclose(t);
    sDumping = 0;
}

static void DumpAtExit(void)
{
    Dump("exit");
}

static void Start(void)
{
    const char *out = getenv("FE8PC_UBSAN_OUT");

    sStarted = 1;
    if (out != NULL && *out != '\0')
    {
        snprintf(sOutPath, sizeof(sOutPath), "%s", out);
        atexit(DumpAtExit);
    }
    PortLog("[ubsan] UBSan sweep binary: each check's first hit is logged here; table: %s\n",
        sOutPath[0] ? sOutPath : "none (FE8PC_UBSAN_OUT not set)");
}

/* The site of one hit, NULL when it is not new (its count is kept anyway).
   noinline: the backtrace below starts in it. */
static __attribute__((noinline)) struct Site *Hit(const void *key, const struct SourceLocation *loc,
    const char *check, void *ret)
{
    uint32_t tick = CurrentTick(), h;
    unsigned i, probes;
    struct Site *s = NULL;

    if (!sStarted)
        Start();
    sHits++;
    h = (uint32_t)(uintptr_t)key * 2654435761u;
    /* A site is the check's data and the kind of failure: one data block can
       serve several kinds (a shift's exponent and base, an access's NULL,
       alignment and size checks), each counted on its own. */
    for (i = h >> 20, probes = 0; probes < SITE_CAP; i = (i + 1) & (SITE_CAP - 1), probes++)
    {
        if (sSites[i].key == key && sSites[i].check == check)
        {
            s = &sSites[i];
            s->count++;
            s->lastTick = tick;
            if (tick >= sNextDumpTick)
            {
                sNextDumpTick = tick + DUMP_EVERY;
                Dump("periodic");
            }
            return NULL;
        }
        if (sSites[i].key == NULL)
            break;
    }
    if (probes == SITE_CAP || sSiteCount >= SITE_CAP * 3 / 4)
    {
        sDroppedHits++; /* the table is full: counted, not kept */
        return NULL;
    }
    s = &sSites[i];
    s->key = key;
    s->check = check;
    s->loc = *loc;
    s->id = ++sSiteCount;
    s->count = 1;
    s->firstTick = s->lastTick = tick;
    {
        void *frames[SITE_BT + 4];
        int n = backtrace(frames, SITE_BT + 4), k, from = 1;

        for (k = 0; k < n; k++)
            if (frames[k] == ret)
            {
                from = k;
                break;
            }
        for (k = from; k < n && s->nbt < SITE_BT; k++)
            s->bt[s->nbt++] = frames[k];
    }
    return s;
}

static void Report(struct Site *s)
{
    char where[160];

    NameAddr(s->nbt > 0 ? (uintptr_t)s->bt[0] : 0, where, sizeof(where));
    PortLog("[ubsan] site %u (tick %u): %s:%u:%u: %s: %s [in %s]\n", s->id, s->firstTick,
        s->loc.file ? s->loc.file : "?", s->loc.line, s->loc.column, s->check, s->msg, where);
    if (s->firstTick >= sNextDumpTick)
    {
        sNextDumpTick = s->firstTick + DUMP_EVERY;
        Dump("periodic");
    }
}

static void Fatal(const char *what)
{
    PortLog("[fault] ubsan: %s; the run stops here (port/test/pc/ubsan_log.c)\n", what);
    Dump("fatal");
    _exit(6);
}

#define RET __builtin_return_address(0)

/* ---- the handlers ---- */

static const char *const kTypeCheckKinds[] = {
    "load of", "store to", "reference binding to", "member access within", "member call on",
    "constructor call on", "downcast of", "downcast of", "upcast of", "cast to virtual base of",
    "_Nonnull binding to", "dynamic operation on"};

static void TypeMismatch(struct TypeMismatchData *d, ValueHandle ptr, void *ret)
{
    uintptr_t align = (uintptr_t)1 << d->logAlignment;
    const char *what = d->typeCheckKind < sizeof(kTypeCheckKinds) / sizeof(kTypeCheckKinds[0])
        ? kTypeCheckKinds[d->typeCheckKind] : "access of";
    const char *check = ptr == 0 ? "null" : (ptr & (align - 1)) ? "alignment" : "object-size";
    struct Site *s = Hit(d, &d->loc, check, ret);

    if (s == NULL)
        return;
    if (ptr == 0)
        snprintf(s->msg, SITE_MSG, "%s null pointer of type %s", what, TypeName(d->type));
    else if (ptr & (align - 1))
        snprintf(s->msg, SITE_MSG, "%s misaligned address 0x%08x for type %s, which requires %u byte alignment",
            what, (unsigned)ptr, TypeName(d->type), (unsigned)align);
    else
        snprintf(s->msg, SITE_MSG, "%s address 0x%08x with insufficient space for an object of type %s", what,
            (unsigned)ptr, TypeName(d->type));
    Report(s);
}

void __ubsan_handle_type_mismatch_v1(struct TypeMismatchData *d, ValueHandle ptr)
{
    TypeMismatch(d, ptr, RET);
}

void __ubsan_handle_type_mismatch_v1_abort(struct TypeMismatchData *d, ValueHandle ptr)
{
    TypeMismatch(d, ptr, RET);
}

static void Overflow(struct OverflowData *d, ValueHandle l, ValueHandle r, char op, void *ret)
{
    struct Site *s = Hit(d, &d->loc, IsSignedInt(d->type) ? "signed-integer-overflow" : "unsigned-integer-overflow",
        ret);
    char a[48], b[48];

    if (s == NULL)
        return;
    FormatValue(a, sizeof(a), d->type, l);
    FormatValue(b, sizeof(b), d->type, r);
    snprintf(s->msg, SITE_MSG, "%s integer overflow: %s %c %s cannot be represented in type %s",
        IsSignedInt(d->type) ? "signed" : "unsigned", a, op, b, TypeName(d->type));
    Report(s);
}

void __ubsan_handle_add_overflow(struct OverflowData *d, ValueHandle l, ValueHandle r)
{
    Overflow(d, l, r, '+', RET);
}

void __ubsan_handle_add_overflow_abort(struct OverflowData *d, ValueHandle l, ValueHandle r)
{
    Overflow(d, l, r, '+', RET);
}

void __ubsan_handle_sub_overflow(struct OverflowData *d, ValueHandle l, ValueHandle r)
{
    Overflow(d, l, r, '-', RET);
}

void __ubsan_handle_sub_overflow_abort(struct OverflowData *d, ValueHandle l, ValueHandle r)
{
    Overflow(d, l, r, '-', RET);
}

void __ubsan_handle_mul_overflow(struct OverflowData *d, ValueHandle l, ValueHandle r)
{
    Overflow(d, l, r, '*', RET);
}

void __ubsan_handle_mul_overflow_abort(struct OverflowData *d, ValueHandle l, ValueHandle r)
{
    Overflow(d, l, r, '*', RET);
}

static void Negate(struct OverflowData *d, ValueHandle v, void *ret)
{
    struct Site *s = Hit(d, &d->loc, IsSignedInt(d->type) ? "signed-integer-overflow" : "unsigned-integer-overflow",
        ret);
    char a[48];

    if (s == NULL)
        return;
    FormatValue(a, sizeof(a), d->type, v);
    snprintf(s->msg, SITE_MSG, "negation of %s cannot be represented in type %s", a, TypeName(d->type));
    Report(s);
}

void __ubsan_handle_negate_overflow(struct OverflowData *d, ValueHandle v)
{
    Negate(d, v, RET);
}

void __ubsan_handle_negate_overflow_abort(struct OverflowData *d, ValueHandle v)
{
    Negate(d, v, RET);
}

static void DivRem(struct OverflowData *d, ValueHandle l, ValueHandle r, void *ret)
{
    int minusOne = IsSignedInt(d->type) && SIntValue(d->type, r) == -1;
    const char *check = minusOne ? "signed-integer-overflow"
        : d->type != NULL && d->type->kind == 1 ? "float-divide-by-zero" : "integer-divide-by-zero";
    struct Site *s = Hit(d, &d->loc, check, ret);
    char a[48];

    if (s == NULL)
        return;
    FormatValue(a, sizeof(a), d->type, l);
    if (minusOne)
        snprintf(s->msg, SITE_MSG, "division of %s by -1 cannot be represented in type %s", a, TypeName(d->type));
    else
        snprintf(s->msg, SITE_MSG, "division by zero (%s / 0, type %s)", a, TypeName(d->type));
    Report(s);
}

void __ubsan_handle_divrem_overflow(struct OverflowData *d, ValueHandle l, ValueHandle r)
{
    DivRem(d, l, r, RET);
}

void __ubsan_handle_divrem_overflow_abort(struct OverflowData *d, ValueHandle l, ValueHandle r)
{
    DivRem(d, l, r, RET);
}

static void Shift(struct ShiftOutOfBoundsData *d, ValueHandle l, ValueHandle r, void *ret)
{
    int rhsNeg = IsNegative(d->rhsType, r);
    int tooBig = !rhsNeg && d->lhsType != NULL && d->lhsType->kind == 0 && d->rhsType != NULL
        && d->rhsType->kind == 0 && UIntValue(d->rhsType, r) >= IntWidth(d->lhsType);
    struct Site *s = Hit(d, &d->loc, rhsNeg || tooBig ? "shift-exponent" : "shift-base", ret);
    char a[48], b[48];

    if (s == NULL)
        return;
    FormatValue(a, sizeof(a), d->lhsType, l);
    FormatValue(b, sizeof(b), d->rhsType, r);
    if (rhsNeg)
        snprintf(s->msg, SITE_MSG, "shift exponent %s is negative (of %s, type %s)", b, a, TypeName(d->lhsType));
    else if (tooBig)
        snprintf(s->msg, SITE_MSG, "shift exponent %s is too large for %u-bit type %s (shifting %s)", b,
            IntWidth(d->lhsType), TypeName(d->lhsType), a);
    else if (IsNegative(d->lhsType, l))
        snprintf(s->msg, SITE_MSG, "left shift of negative value %s (by %s, type %s)", a, b, TypeName(d->lhsType));
    else
        snprintf(s->msg, SITE_MSG, "left shift of %s by %s places cannot be represented in type %s", a, b,
            TypeName(d->lhsType));
    Report(s);
}

void __ubsan_handle_shift_out_of_bounds(struct ShiftOutOfBoundsData *d, ValueHandle l, ValueHandle r)
{
    Shift(d, l, r, RET);
}

void __ubsan_handle_shift_out_of_bounds_abort(struct ShiftOutOfBoundsData *d, ValueHandle l, ValueHandle r)
{
    Shift(d, l, r, RET);
}

static void OutOfBounds(struct OutOfBoundsData *d, ValueHandle idx, void *ret)
{
    struct Site *s = Hit(d, &d->loc, "bounds", ret);
    char a[48];

    if (s == NULL)
        return;
    FormatValue(a, sizeof(a), d->indexType, idx);
    snprintf(s->msg, SITE_MSG, "index %s out of bounds for type %s", a, TypeName(d->arrayType));
    Report(s);
}

void __ubsan_handle_out_of_bounds(struct OutOfBoundsData *d, ValueHandle idx)
{
    OutOfBounds(d, idx, RET);
}

void __ubsan_handle_out_of_bounds_abort(struct OutOfBoundsData *d, ValueHandle idx)
{
    OutOfBounds(d, idx, RET);
}

static void VlaBound(struct VLABoundData *d, ValueHandle bound, void *ret)
{
    struct Site *s = Hit(d, &d->loc, "vla-bound", ret);
    char a[48];

    if (s == NULL)
        return;
    FormatValue(a, sizeof(a), d->type, bound);
    snprintf(s->msg, SITE_MSG, "variable length array bound evaluates to non-positive value %s", a);
    Report(s);
}

void __ubsan_handle_vla_bound_not_positive(struct VLABoundData *d, ValueHandle bound)
{
    VlaBound(d, bound, RET);
}

void __ubsan_handle_vla_bound_not_positive_abort(struct VLABoundData *d, ValueHandle bound)
{
    VlaBound(d, bound, RET);
}

static void InvalidValue(struct InvalidValueData *d, ValueHandle v, void *ret)
{
    struct Site *s = Hit(d, &d->loc, "bool-enum", ret);
    char a[48];

    if (s == NULL)
        return;
    FormatValue(a, sizeof(a), d->type, v);
    snprintf(s->msg, SITE_MSG, "load of value %s, which is not a valid value for type %s", a, TypeName(d->type));
    Report(s);
}

void __ubsan_handle_load_invalid_value(struct InvalidValueData *d, ValueHandle v)
{
    InvalidValue(d, v, RET);
}

void __ubsan_handle_load_invalid_value_abort(struct InvalidValueData *d, ValueHandle v)
{
    InvalidValue(d, v, RET);
}

static void NonNullArg(struct NonNullArgData *d, void *ret)
{
    struct Site *s = Hit(d, &d->loc, "nonnull-attribute", ret);

    if (s == NULL)
        return;
    if (d->attrLoc.file != NULL)
        snprintf(s->msg, SITE_MSG, "null pointer passed as argument %d, which is declared to never be null (%s:%u)",
            d->argIndex, d->attrLoc.file, d->attrLoc.line);
    else
        snprintf(s->msg, SITE_MSG, "null pointer passed as argument %d, which is declared to never be null",
            d->argIndex);
    Report(s);
}

void __ubsan_handle_nonnull_arg(struct NonNullArgData *d)
{
    NonNullArg(d, RET);
}

void __ubsan_handle_nonnull_arg_abort(struct NonNullArgData *d)
{
    NonNullArg(d, RET);
}

static void NonNullReturn(struct NonNullReturnData *d, struct SourceLocation *loc, void *ret)
{
    struct Site *s = Hit(loc, loc, "returns-nonnull-attribute", ret);

    if (s == NULL)
        return;
    snprintf(s->msg, SITE_MSG, "null pointer returned from function declared to never return null (%s:%u)",
        d->attrLoc.file ? d->attrLoc.file : "?", d->attrLoc.line);
    Report(s);
}

void __ubsan_handle_nonnull_return_v1(struct NonNullReturnData *d, struct SourceLocation *loc)
{
    NonNullReturn(d, loc, RET);
}

void __ubsan_handle_nonnull_return_v1_abort(struct NonNullReturnData *d, struct SourceLocation *loc)
{
    NonNullReturn(d, loc, RET);
}

static void PointerOverflow(struct PointerOverflowData *d, ValueHandle base, ValueHandle result, void *ret)
{
    struct Site *s = Hit(d, &d->loc, "pointer-overflow", ret);

    if (s == NULL)
        return;
    if (base == 0 && result == 0)
        snprintf(s->msg, SITE_MSG, "applying zero offset to null pointer");
    else if (base == 0)
        snprintf(s->msg, SITE_MSG, "applying non-zero offset 0x%08x to null pointer", (unsigned)result);
    else if (result == 0)
        snprintf(s->msg, SITE_MSG, "applying non-zero offset to non-null pointer 0x%08x produced null pointer",
            (unsigned)base);
    else
        snprintf(s->msg, SITE_MSG, "pointer index expression with base 0x%08x overflowed to 0x%08x",
            (unsigned)base, (unsigned)result);
    Report(s);
}

void __ubsan_handle_pointer_overflow(struct PointerOverflowData *d, ValueHandle base, ValueHandle result)
{
    PointerOverflow(d, base, result, RET);
}

void __ubsan_handle_pointer_overflow_abort(struct PointerOverflowData *d, ValueHandle base, ValueHandle result)
{
    PointerOverflow(d, base, result, RET);
}

static void InvalidBuiltin(struct InvalidBuiltinData *d, void *ret)
{
    struct Site *s = Hit(d, &d->loc, "builtin", ret);

    if (s == NULL)
        return;
    snprintf(s->msg, SITE_MSG, "passing zero to __builtin_%s(), which is not a valid argument",
        d->kind == 0 ? "ctz" : "clz");
    Report(s);
}

void __ubsan_handle_invalid_builtin(struct InvalidBuiltinData *d)
{
    InvalidBuiltin(d, RET);
}

void __ubsan_handle_invalid_builtin_abort(struct InvalidBuiltinData *d)
{
    InvalidBuiltin(d, RET);
}

/* Only with -fsanitize=float-cast-overflow (not part of "undefined" in GCC). */
static void FloatCast(struct FloatCastOverflowData *d, ValueHandle from, void *ret)
{
    struct Site *s = Hit(d, &d->loc, "float-cast-overflow", ret);
    char a[48];

    if (s == NULL)
        return;
    FormatValue(a, sizeof(a), d->fromType, from);
    snprintf(s->msg, SITE_MSG, "%s is outside the range of representable values of type %s", a,
        TypeName(d->toType));
    Report(s);
}

void __ubsan_handle_float_cast_overflow(struct FloatCastOverflowData *d, ValueHandle from)
{
    FloatCast(d, from, RET);
}

void __ubsan_handle_float_cast_overflow_abort(struct FloatCastOverflowData *d, ValueHandle from)
{
    FloatCast(d, from, RET);
}

/* GCC emits nothing after these calls, so they cannot return. */
void __ubsan_handle_builtin_unreachable(struct UnreachableData *d)
{
    struct Site *s = Hit(d, &d->loc, "unreachable", RET);

    if (s != NULL)
    {
        snprintf(s->msg, SITE_MSG, "execution reached an unreachable program point");
        Report(s);
    }
    Fatal("execution reached __builtin_unreachable");
}

void __ubsan_handle_missing_return(struct UnreachableData *d)
{
    struct Site *s = Hit(d, &d->loc, "return", RET);

    if (s != NULL)
    {
        snprintf(s->msg, SITE_MSG, "execution reached the end of a value-returning function without returning a value");
        Report(s);
    }
    Fatal("a value-returning function ran off its end");
}
