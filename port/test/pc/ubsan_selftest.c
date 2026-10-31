/*
 * Self-test of the UBSan sweep's runtime (port/test/pc/ubsan_log.c), run by
 * port/test/tools/ubsan_selftest.py (make -f port/test/sweep.mk ubsan-selftest):
 * one failing check of each kind GCC's -fsanitize=undefined,bounds emits,
 * each hit twice. The program is linked once with ubsan_log.c and once with
 * GCC's own libubsan, and the two reports must name the same sites with the
 * same messages, and ubsan_log.c must count 2 hits at each. A stand-alone host
 * program: no game code, no ROM.
 *
 * Not tested: integer division by zero and INT_MIN / -1 (x86's idiv traps
 * right after the report, in both runtimes), __builtin_unreachable (stops the
 * run by design) and NULL loads (fault after the report).
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct S
{
    int a;
    int arr[3];
    int tail;
};

struct Big
{
    int x[64];
};

/* What ubsan_log.c needs from the port and the Gate 0 hook. */
void PortLog(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

uint32_t PortGate0Tick(void)
{
    return 7;
}

/* One check per line: the self-test matches the reports by line and column. */
__attribute__((noinline)) int Add(int x, int y) { return x + y; }
__attribute__((noinline)) int Sub(int x, int y) { return x - y; }
__attribute__((noinline)) int Mul(int x, int y) { return x * y; }
__attribute__((noinline)) int Neg(int x) { return -x; }
__attribute__((noinline)) int Shl(int x, int s) { return x << s; }
__attribute__((noinline)) unsigned Shr(unsigned x, int s) { return x >> s; }
__attribute__((noinline)) int Index(struct S *p, int i) { return p->arr[i]; }
__attribute__((noinline)) int Load(int *p) { return *p; }
__attribute__((noinline)) int Bool(_Bool *p) { return *p ? 1 : 2; }
__attribute__((noinline)) int Vla(int n) { int a[n]; a[0] = 1; return a[0] + n; }
__attribute__((noinline)) int Clz(unsigned x) { return __builtin_clz(x); }
__attribute__((noinline)) char *PtrOverflow(char *p, uintptr_t off) { return p + off; }
__attribute__((noinline)) long long Add64(long long x, long long y) { return x + y; }
__attribute__((noinline)) void *NonNull(void *d, const void *s, size_t n) { return memcpy(d, s, n); }

int main(int argc, char **argv)
{
    static char buf[16];
    static struct S s;
    volatile int big = 0x7fffffff, zero = 0;
    volatile unsigned char notBool = 7;
    volatile long long big64 = 0x7fffffffffffffffLL;
    char small[4] = {0};
    int i;

    (void)argv;
    for (i = 0; i < 2; i++)
    {
        printf("%d\n", Add(big, 1));
        printf("%d\n", Sub(-big, 2));
        printf("%d\n", Mul(big, 3));
        printf("%d\n", Neg(-big - 1));
        printf("%d\n", Shl(1, 32 + argc));
        printf("%d\n", Shl(1 << 30, 3));
        printf("%u\n", Shr(1, -argc));
        printf("%d\n", Index(&s, 3 + argc));
        printf("%d\n", Load((int *)(buf + 1)));
        printf("%d\n", Bool((_Bool *)&notBool));
        printf("%d\n", Vla(zero));
        printf("%d\n", Clz(zero));
        printf("%p\n", (void *)PtrOverflow(buf, (uintptr_t)-0x10000000));
        printf("%lld\n", Add64(big64, 1));
        NonNull(buf, NULL, zero);
        printf("%d\n", ((struct Big *)small)->x[i + 62]);
    }
    return 0;
}
