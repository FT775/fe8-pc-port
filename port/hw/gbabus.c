#include <stdint.h>

#include "port_hw.h"
#include "port_gbabus.h"

/*
 * GBA data loads (and a byte store) at a GBA address (port_gbabus.h). The
 * host maps EWRAM, IWRAM, I/O, palette, VRAM, OAM, ROM and SRAM once each, at
 * their GBA addresses (port/hw/memory.c). The GBA also answers on their
 * mirrors and on unmapped space; this file gives the GBA's value there by
 * reading the mapped copy, the open bus or the BIOS latch, and stores where
 * the GBA stores, never touching an unmapped host page.
 */

static uint32_t Ror32(uint32_t v, unsigned n)
{
    n &= 31;
    return n ? (v >> n) | (v << (32 - n)) : v;
}

static uint32_t HostWord(uint32_t hostAddr)
{
    return *(const volatile uint32_t *)(uintptr_t)hostAddr;
}

/* The 32-bit word the GBA bus gives for an access at addr & ~3. */
static uint32_t BusWord(uint32_t addr, uint32_t openBus, uint32_t biosLatch)
{
    uint32_t a = addr & ~3u;
    uint32_t off;

    switch (a >> 24)
    {
    case 0x00:
        /* BIOS: the latch while code runs outside it; open bus above 0x4000. */
        return a < 0x4000 ? biosLatch : openBus;
    case 0x02:
        return HostWord(PORT_EWRAM_START | (a & 0x3FFFF)); /* 256 KiB, mirrored */
    case 0x03:
        return HostWord(PORT_IWRAM_START | (a & 0x7FFF)); /* 32 KiB, mirrored */
    case 0x04:
        /* Not reached by any guarded load so far. The host's register image
           for the first 1 KiB (a write-only register reads back what was
           written, not the GBA's value), open bus above it. */
        return (a & 0xFFFFFF) < 0x400 ? HostWord(PORT_IO_START | (a & 0x3FF)) : openBus;
    case 0x05:
        return HostWord(PORT_PLTT_START | (a & 0x3FF)); /* 1 KiB, mirrored */
    case 0x06:
        off = a & 0x1FFFF; /* 96 KiB in a 128 KiB mirror; 0x18000-0x1FFFF = 0x10000-0x17FFF */
        if (off >= 0x18000)
            off -= 0x8000;
        return HostWord(PORT_VRAM_START | off);
    case 0x07:
        return HostWord(PORT_OAM_START | (a & 0x3FF)); /* 1 KiB, mirrored */
    case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D:
        /* Three wait-state windows of one 32 MiB cartridge space. */
        off = a & (PORT_ROM_MAX - 1);
        if (off < PortRomSize())
            return PortRomGbaWord(PORT_ROM_START + off);
        /* Past the ROM image nothing drives the bus, which keeps the halfword
           address of each 16-bit access (GBATEK, GamePak with no ROM there;
           mGBA's out-of-range cartridge load). A 32-bit load is two accesses,
           at a and a + 2. Whether a 16 MiB retail cart mirrors its ROM here
           instead is not known (port-plan/pending/merged/groupF-design.md). */
        return ((a >> 1) & 0xFFFF) | ((((a + 2) >> 1) & 0xFFFF) << 16);
    case 0x0E: case 0x0F:
        /* 32 KiB of 8-bit SRAM, mirrored: the addressed byte on every lane. */
        return 0x01010101u * *(const volatile uint8_t *)(uintptr_t)(PORT_SRAM_START | (addr & 0x7FFF));
    default:
        return openBus; /* 0x01xxxxxx and 0x10000000 up: nothing answers */
    }
}

uint32_t PortGbaLoad32(uint32_t addr, uint32_t openBus, uint32_t biosLatch)
{
    return Ror32(BusWord(addr, openBus, biosLatch), (addr & 3) * 8);
}

uint32_t PortGbaLoad16(uint32_t addr, uint32_t openBus, uint32_t biosLatch)
{
    uint32_t v = (BusWord(addr, openBus, biosLatch) >> ((addr & 2) * 8)) & 0xFFFF;

    /* Zero-extended, as LDRH on the ARM7TDMI. (mGBA 0.10.5 differs past a
       16 MiB image: a cartridge halfword load whose low 24 address bits are
       below 0xFE0000 goes through its AGB Print emulation, and _agbPrintLoad
       returns int16_t, so the value is sign-extended when address bit 16 is
       set. An emulator effect, an oracle limit, not the GBA.) */
    return Ror32(v, (addr & 1) * 8);
}

uint32_t PortGbaLoad8(uint32_t addr, uint32_t openBus, uint32_t biosLatch)
{
    return (BusWord(addr, openBus, biosLatch) >> ((addr & 3) * 8)) & 0xFF;
}

/*
 * A byte store (STRB) at a GBA address, as the GBA bus takes it (GBATEK
 * "GBA Memory Map" and "Writing 8bit Data to Video Memory"; mGBA's GBAStore8
 * does the same): EWRAM and IWRAM on their mirrors; palette RAM and BG VRAM
 * get the byte in both halves of the addressed halfword; OBJ VRAM (from
 * 0x10000, 0x14000 in the bitmap modes 3-5) and OAM ignore it; SRAM takes the
 * byte on its mirror. The BIOS area, unused space, the cartridge windows (no
 * GPIO on this cart) and 0x10000000 up ignore it, so nothing is stored (the
 * H-1 fault handler drops a host store below 0x4000 too, but only there).
 * Not modelled: I/O, where the GBA's register write can have side effects (an
 * interrupt acknowledge, a DMA start); the byte goes to the host's register
 * image for the first 1 KiB, as the game's own I/O stores do, and is dropped
 * above it. No guarded store reaches I/O, the video memories or SRAM so far.
 */
void PortGbaStore8(uint32_t addr, uint8_t value)
{
    uint32_t off;

    switch (addr >> 24)
    {
    case 0x02:
        *(volatile uint8_t *)(uintptr_t)(PORT_EWRAM_START | (addr & 0x3FFFF)) = value;
        break;
    case 0x03:
        *(volatile uint8_t *)(uintptr_t)(PORT_IWRAM_START | (addr & 0x7FFF)) = value;
        break;
    case 0x04:
        if ((addr & 0xFFFFFF) < 0x400)
            *(volatile uint8_t *)(uintptr_t)(PORT_IO_START | (addr & 0x3FF)) = value;
        break;
    case 0x05:
        *(volatile uint16_t *)(uintptr_t)(PORT_PLTT_START | (addr & 0x3FE)) = (uint16_t)(value * 0x0101u);
        break;
    case 0x06:
        /* DISPCNT's mode bits in the host's register image */
        off = addr & 0x1FFFF;
        if (off < (((*(const volatile uint16_t *)(uintptr_t)PORT_IO_START) & 7) >= 3 ? 0x14000u : 0x10000u))
            *(volatile uint16_t *)(uintptr_t)(PORT_VRAM_START | (off & ~1u)) = (uint16_t)(value * 0x0101u);
        break;
    case 0x0E: case 0x0F:
        *(volatile uint8_t *)(uintptr_t)(PORT_SRAM_START | (addr & 0x7FFF)) = value;
        break;
    default:
        break; /* BIOS area, unused space, OAM, cartridge ROM, 0x10000000 up */
    }
}
