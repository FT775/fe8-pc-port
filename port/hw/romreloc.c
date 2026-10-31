#include <stdint.h>

#include "port_hw.h"
#include "port_gbabus.h" /* PortRomGbaAddressOf */

/*
 * Plan risk 12: code pointers in ROM data. The host reads ROM data in place
 * (D1-a), so a GBA code pointer in, say, a proc script would be called as a
 * GBA address and crash. port/tools/fixptr.py lists every ROM word that the
 * GBA link relocated (each one checked against the ROM) and whose host value
 * differs: code pointers, and pointers to data the host builds itself. The
 * table is generated into the build directory (romreloc.c there). This file
 * applies it to the private copy of the ROM after it is loaded.
 */

struct PortRomReloc
{
    uint32_t site;      /* GBA address of the ROM word */
    uint32_t gbaValue;  /* the word as the GBA ROM has it */
    const void *target; /* host symbol */
    int32_t addend;
};

extern const struct PortRomReloc gPortRomRelocs[];
extern const unsigned gPortRomRelocCount;

void PortApplyRomRelocs(void)
{
    unsigned i, patched = 0, same = 0, mismatch = 0;
    uint32_t romEnd = PORT_ROM_START + PortRomSize();

    for (i = 0; i < gPortRomRelocCount; i++)
    {
        const struct PortRomReloc *r = &gPortRomRelocs[i];
        uint32_t *word = (uint32_t *)(uintptr_t)r->site;
        uint32_t want = (uint32_t)(uintptr_t)r->target + (uint32_t)r->addend;

        if (r->site < PORT_ROM_START || r->site + 4 > romEnd || *word != r->gbaValue)
        {
            /* Not the ROM the table was made from: leave the word alone. */
            if (mismatch++ < 4)
                PortLog("[reloc] ROM word at 0x%08X is not 0x%08X, left alone\n", r->site, r->gbaValue);
            continue;
        }
        if (*word == want)
        {
            same++;
            continue;
        }
        *word = want;
        patched++;
    }
    PortLog("[reloc] %u ROM pointer words in the table: %u now point at host code or data, "
            "%u already right, %u left alone (ROM mismatch)\n",
        gPortRomRelocCount, patched, same, mismatch);
}

/* The aligned ROM word at GBA address site (inside the image) as the GBA ROM
   has it: the host copy, or the table's gbaValue where PortApplyRomRelocs
   rewrote the word (port/hw/gbabus.c; the table is sorted by site). */
uint32_t PortRomGbaWord(uint32_t site)
{
    uint32_t host = *(const uint32_t *)(uintptr_t)site;
    unsigned lo = 0, hi = gPortRomRelocCount;

    while (lo < hi)
    {
        unsigned mid = lo + (hi - lo) / 2;
        const struct PortRomReloc *r = &gPortRomRelocs[mid];

        if (r->site < site)
            lo = mid + 1;
        else if (r->site > site)
            hi = mid;
        else
            return host == (uint32_t)(uintptr_t)r->target + (uint32_t)r->addend ? r->gbaValue : host;
    }
    return host;
}

/* The GBA address of the host-built ROM object that starts at host address p
   (port_gbabus.h): the gbaValue of a table entry that points at the start of
   that host symbol (target p, addend 0). fixptr.py checked every entry against
   the ROM, and each host symbol stands for one GBA symbol, so every such entry
   has the same gbaValue. Entries with an addend are not used: a pointer past
   the end of one host object can equal the start of the next, whose GBA
   address is not that pointer's. 0 when no ROM word points at p's symbol. */
uint32_t PortRomGbaAddressOf(const void *p)
{
    unsigned i;

    for (i = 0; i < gPortRomRelocCount; i++)
    {
        const struct PortRomReloc *r = &gPortRomRelocs[i];

        if (r->target == p && r->addend == 0)
            return r->gbaValue;
    }
    return 0;
}
