#ifndef GUARD_PORT_PORT_GBABUS_H
#define GUARD_PORT_PORT_GBABUS_H

/*
 * GBA data loads at a 32-bit GBA address (port/hw/gbabus.c), for guarded
 * fixes (H-3, port-plan/DIVERGENCES.md) whose game code reads through a wild
 * pointer and must get what the GBA bus returns there: RAM mirrors, the BIOS
 * latch, the CPU open bus, ROM past the image. Under D1-a the host maps each
 * GBA memory once, at its GBA address; these loads never touch a host page
 * that is not mapped. port.h includes this file, so the game files that use
 * it need no #include of their own. Port-only code; the GBA build never sees
 * it.
 *
 * openBus:   what the CPU's own prefetch leaves on the bus at that load. For
 *            THUMB code in ROM (all the game's C) it is the halfword at the
 *            load instruction's address + 4, in both halves (GBATEK "Reading
 *            from Unused Memory"; mGBA GBALoadBad), so each load has its own.
 *            Oracle limit: mGBA 0.10.5 returns its stale gba->bus word instead
 *            while a DMA runs or when the load is the instruction right after
 *            the one its last DMA update saw (gba->dmaPC). Because dmaPC stays
 *            until the next DMA (a sound DMA every 16 lines), every pass of a
 *            loop through that instruction gets it. That is cycle timing the
 *            port does not model (port-plan/DIVERGENCES.md OL-7).
 * biosLatch: the word a load below 0x4000 returns: PortOpenBusLatch() (port.h,
 *            the "Open-bus latch rule"), or a site's measured value.
 *
 * Results are what the ARM7TDMI gives: LDR rotates a misaligned word, LDRH
 * zero-extends the halfword and rotates it by 8 at an odd address, LDRB
 * zero-extends.
 */

#include <stdint.h>

uint32_t PortGbaLoad32(uint32_t addr, uint32_t openBus, uint32_t biosLatch);
uint32_t PortGbaLoad16(uint32_t addr, uint32_t openBus, uint32_t biosLatch);
uint32_t PortGbaLoad8(uint32_t addr, uint32_t openBus, uint32_t biosLatch);

/* A byte store (STRB) at a 32-bit GBA address, as the GBA bus takes it: RAM on
   its mirrors, the video memories' byte-store rules, SRAM, and no store at all
   in the BIOS area, unused space, OAM and ROM (port/hw/gbabus.c). For guarded
   fixes whose game code stores through a wild pointer (PortBmMapWrite, port.h). */
void PortGbaStore8(uint32_t addr, uint8_t value);

/* The GBA address of an object in ROM data that the host builds itself, from
   the host address p of its start: the GBA's value of a ROM pointer word that
   romreloc.c rewrote to point at p (port/hw/romreloc.c), or 0 when no ROM word
   points there. A linear search of the relocation table: rare paths only
   (PortRomTableLoad8, port.h). */
uint32_t PortRomGbaAddressOf(const void *p);

/* The open-bus word of a THUMB load in ROM whose address + 4 holds opcode op. */
#define PORT_THUMB_OPEN_BUS(op) ((uint32_t)(op) * 0x00010001u)

/*
 * The BIOS latch (port-plan/DIVERGENCES.md, "Open-bus latch rule"): a load
 * from 0x00000000-0x00003FFF returns the last opcode the BIOS fetched,
 * 0xE3A02004 after a SWI, 0xE55EC002 after an IRQ (GBATEK, BIOS 0x188+8 and
 * 0x13C+8). The rule takes post-IRQ while the HBlank IRQ is on (IME bit 0, IE
 * bit 1 and DISPSTAT bit 4: one IRQ per scanline, so one lands after any SWI),
 * else post-SWI. This is the only copy of the rule: PortOpenBusLatch (port.h,
 * the game's guards), the NULL probe's LatchRule (port/hw/nullprobe.c) and the
 * flood's FloodLoad8 (port/asm/arm_pc.c, built without port.h) all use these
 * three, so they cannot drift apart.
 */
#define PORT_OPEN_BUS_SWI 0xE3A02004u
#define PORT_OPEN_BUS_IRQ 0xE55EC002u
#define PORT_OPEN_BUS_HBLANK_IRQ_ON(ime, ie, dispstat) \
    ((((ime) & 1) != 0) && (((ie) & 0x0002) != 0) && (((dispstat) & 0x0010) != 0))

#endif /* GUARD_PORT_PORT_GBABUS_H */
