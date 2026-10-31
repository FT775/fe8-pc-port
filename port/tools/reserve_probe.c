/*
 * reserve_probe.c - D1-a feasibility probe (port-only, C99).
 *
 * Tries to reserve every GBA memory region at its GBA address in a 32-bit
 * process and reports what happened. Build it for each host:
 *   Linux i386 : gcc -m32 -O1 -o reserve_probe port/tools/reserve_probe.c
 *   Win32      : i686-w64-mingw32-gcc -O1 -o reserve_probe.exe port/tools/reserve_probe.c
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <errno.h>
#endif

struct Region
{
    const char *name;
    uint32_t addr;
    uint32_t size;
};

static const struct Region sRegions[] = {
    { "EWRAM", 0x02000000, 0x00040000 },
    { "IWRAM", 0x03000000, 0x00008000 },
    { "IO", 0x04000000, 0x00001000 },
    { "PLTT", 0x05000000, 0x00001000 },
    { "VRAM", 0x06000000, 0x00018000 },
    { "OAM", 0x07000000, 0x00001000 },
    { "ROM", 0x08000000, 0x02000000 },
    { "SRAM", 0x0E000000, 0x00010000 },
};

int main(void)
{
    int failures = 0;
    unsigned i;

    printf("pointer size: %u bytes\n", (unsigned)sizeof(void *));
    for (i = 0; i < sizeof(sRegions) / sizeof(sRegions[0]); i++)
    {
        const struct Region *r = &sRegions[i];
        void *want = (void *)(uintptr_t)r->addr;
        void *got;
#ifdef _WIN32
        got = VirtualAlloc(want, r->size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (got != want)
        {
            MEMORY_BASIC_INFORMATION mbi;
            memset(&mbi, 0, sizeof(mbi));
            VirtualQuery(want, &mbi, sizeof(mbi));
            printf("%-6s 0x%08X +0x%07X FAIL (got %p, GetLastError=%lu, state=0x%lx base=%p "
                   "alloc_base=%p size=0x%lx type=0x%lx)\n",
                r->name, r->addr, r->size, got, GetLastError(), mbi.State, mbi.BaseAddress,
                mbi.AllocationBase, (unsigned long)mbi.RegionSize, mbi.Type);
            failures++;
            continue;
        }
#else
        got = mmap(want, r->size, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (got != want)
        {
            printf("%-6s 0x%08X +0x%07X FAIL (got %p, errno=%d %s)\n", r->name, r->addr,
                r->size, got, errno, strerror(errno));
            failures++;
            continue;
        }
#endif
        /* Touch first and last byte to prove the mapping is usable. */
        ((volatile uint8_t *)got)[0] = 0xA5;
        ((volatile uint8_t *)got)[r->size - 1] = 0x5A;
        printf("%-6s 0x%08X +0x%07X OK\n", r->name, r->addr, r->size);
    }
    printf("main() at %p, stack var at %p\n", (void *)main, (void *)&failures);
    printf("%s\n", failures ? "RESERVATION FAILED" : "ALL REGIONS RESERVED");
    return failures ? 1 : 0;
}
