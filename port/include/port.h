#ifndef GUARD_PORT_PORT_H
#define GUARD_PORT_PORT_H

/*
 * PC port prelude. The host build force-includes this file (gcc -include) in
 * front of every shared src/*.c file, so the shared sources stay unedited.
 *
 * D1-a model: the host is a 32-bit (ILP32) process and the GBA memory map is
 * reserved at its GBA addresses, so REG_*, PLTT, VRAM, OAM, the BIOS slots
 * and 0x08xxxxxx ROM literals all work as plain memory accesses.
 */

#include "gba/types.h"
#include "gba/macro.h"

/*
 * DMA: on the GBA a write to DMAxCNT starts the transfer. On the host the I/O
 * area is plain memory, so a register write does nothing by itself. Route the
 * one macro every Dma* helper is built on to a host function instead.
 */
#undef DmaSet
#define DmaSet(dmaNum, src, dest, control) \
    PortDmaSet((dmaNum), (u32)(src), (u32)(dest), (u32)(control))

void PortDmaSet(int dmaNum, u32 src, u32 dest, u32 control);

/*
 * Header-order fixes. agbcc accepts "extern struct X a[];" while X is still
 * incomplete; modern GCC does not. eventcall.h uses struct REDA (muctrl.h)
 * without including it. Pulling global.h in first is what every file does
 * anyway (CLAUDE.md: global.h is always the first include).
 * Long term this is a one-line include in eventcall.h (a guarded edit).
 */
#include "global.h"
#include "muctrl.h"

/*
 * prelude.h tests PRELUDE_H but never defines it, so every later
 * #include "prelude.h" (types.h, variables.h) would define its macros again.
 * It is included once here and PRELUDE_H is defined, so the changes below stay.
 */
#include "gba/defines.h"
#include "prelude.h"
#ifndef PRELUDE_H
#define PRELUDE_H
#endif

/*
 * RAM placement (DIVERGENCES.md B-12, was P-14). On the GBA, EWRAM_DATA,
 * IWRAM_DATA and EWRAM_OVERLAY(id) put a file's RAM variables in one named
 * section, which the GBA linker script places, and agbcc lays the variables
 * out inside it. GCC lays them out differently: with -fno-toplevel-reorder it
 * emits first the variables whose header declaration carries the attribute,
 * and it aligns each one by its own rule. So the host leaves these macros
 * empty, port/Makefile compiles the game files with -fdata-sections (one
 * section per variable: .bss.NAME, .data.NAME), and port/tools/gen_host_link.py
 * places each variable at its own GBA address, taken from the GBA build.
 * port/tools/check_ram_addrs.py fails the link if any RAM variable is
 * elsewhere. The GBA build keeps the macros (include/gba/defines.h, prelude.h).
 */
#undef EWRAM_DATA
#define EWRAM_DATA
#undef IWRAM_DATA
#define IWRAM_DATA
#undef EWRAM_OVERLAY
#define EWRAM_OVERLAY(id)

/*
 * CONST_DATA (prelude.h) puts "should be const" data in ".data", as agbcc
 * emits it. GCC refuses a file that puts both const and non-const objects in
 * that one named section ("section type conflict"). For files that port/Makefile
 * builds with PORT_CONST_DATA_DEFAULT_SECTION (bmdifficulty.c, DIVERGENCES.md
 * B-7) GCC picks the section instead. The objects and their contents are the
 * same; only where the host keeps its own copy changes (.rodata for const).
 * PRELUDE_H (above) stops a later #include "prelude.h" from restoring the macro.
 */
#ifdef PORT_CONST_DATA_DEFAULT_SECTION
#undef CONST_DATA
#define CONST_DATA
#endif

/*
 * PORT_PLAYED_BUILD: 1 in the builds the player runs, 0 in every test build
 * (port-plan/DECISIONS.md, 2026-10-22). A test build is an FE8PC_TESTBUILD=1
 * build (port/Makefile, DIVERGENCES.md TB-1) or the Gate 0 binary and its
 * sweep builds, whose game code is compiled with FE8PC_DEBUG
 * (port/test/pc/gate0.mk, B-17). The user's deliberate differences for the
 * played builds only (the Link Arena entry, savedraw.c InitSaveMenuChoice)
 * test it, so every test build keeps the GBA's behaviour and the
 * same-buttons runs against the GBA stay exact.
 */
#if defined(FE8PC_TESTBUILD) || defined(FE8PC_DEBUG)
#define PORT_PLAYED_BUILD 0
#else
#define PORT_PLAYED_BUILD 1
#endif

/* GBA data loads at any GBA address (mirrors, BIOS latch, open bus, ROM past
   the image), for guards whose game code reads through a wild pointer; also
   the latch words and the latch rule (PORT_OPEN_BUS_*) used below. */
#include "port_gbabus.h"

/*
 * BIOS-area open bus, for the guarded NULL reads in shared code
 * (port-plan/DIVERGENCES.md, "Open-bus latch rule"). When game code loads
 * from 0x00000000-0x00003FFF, the GBA returns the last opcode the BIOS
 * fetched: 0xE3A02004 after a SWI, 0xE55EC002 after an IRQ (GBATEK, BIOS
 * 0x188+8 and 0x13C+8); a load takes the lanes at addr & 3. The host has no
 * page there. The latch follows the rule (PORT_OPEN_BUS_HBLANK_IRQ_ON,
 * port_gbabus.h, the copy the NULL probe uses too): post-IRQ while the HBlank
 * IRQ is on (IME, IE and DISPSTAT, read at the load), else post-SWI.
 * Not modelled, and not flagged: a lag VBlank IRQ after the last SWI; other
 * IRQ sources (VCount, as in the phase intro, timer, DMA, serial, keypad),
 * which get post-SWI here although the rule does not decide them (the probe
 * marks them "other_irq"); and reads inside an IRQ handler (gPortIrqDepth),
 * where the latch would be 0xE25EF004. Each user's entry says why that does
 * not apply there.
 */
static inline u32 PortOpenBusLatch(void)
{
    if (PORT_OPEN_BUS_HBLANK_IRQ_ON(REG_IME, REG_IE, REG_DISPSTAT))
        return PORT_OPEN_BUS_IRQ;
    return PORT_OPEN_BUS_SWI;
}

/* An aligned load of size 1, 2 or 4 bytes at BIOS-area address addr
   (below 0x4000). A misaligned load, which the ARM rotates, is not modelled. */
static inline u32 PortOpenBusRead(u32 addr, u32 size)
{
    u32 v = PortOpenBusLatch() >> (8 * (addr & 3));

    return size == 1 ? (v & 0xFF) : size == 2 ? (v & 0xFFFF) : v;
}

/* Compile-time check for PORT_NULL_READ (gnu89: a negative array size is an
   error): the member is a 1-, 2- or 4-byte field, naturally aligned, at an
   offset below 0x4000. At 0x4000 and up a load through NULL gets the CPU's
   prefetch open bus, not the BIOS latch (use PortGbaLoad32/16/8 there), and a
   misaligned load is not modelled by PortOpenBusRead. */
#define PORT_NULL_READ_CHECK(off, size)                                                \
    ((void)sizeof(char[((size) == 1 || (size) == 2 || (size) == 4) && (off) < 0x4000 \
                       && ((off) & ((size) - 1)) == 0 ? 1 : -1]))

/* p->member, or what the GBA loads for it when p is NULL (the member's own
   offset, width and sign; the layouts are agbcc's, D-18). */
#define PORT_NULL_READ(p, member)                                                                  \
    (PORT_NULL_READ_CHECK(__builtin_offsetof(__typeof__(*(p)), member), sizeof((p)->member)),      \
     (p) != NULL ? (p)->member                                                                     \
                 : (__typeof__((p)->member))PortOpenBusRead(__builtin_offsetof(__typeof__(*(p)), member), \
                                                            sizeof((p)->member)))

/*
 * A 1-, 2- or 4-byte load at addr as the GBA makes it, whatever 32-bit word
 * addr is (port-plan/DIVERGENCES.md D-74, was working ID D-R10-LUCKY-1): for retail
 * reads through a pointer that can hold any word (a proc field the code never
 * set, which keeps what the slot's last owner left there; the word loaded
 * through NULL by PORT_NULL_READ_VIA). Every value is taken as a GBA address:
 * PortGbaLoad8/16/32 (RAM and its mirrors, ROM as the GBA ROM has it, the BIOS
 * latch by the rule, PortOpenBusLatch, openBus in unused space and from
 * 0x10000000 up; LDRH and LDR rotate at a misaligned address), so no word can
 * fault the PC. A host pointer (the PC's own data, stack or heap) is read as
 * the GBA would read that number, so the callers never pass one: they get GBA
 * RAM, ROM, NULL, a latch word or a never-set word. Not GBA-exact for a word
 * in 0x04xxxxxx: that reads the host's register image (gbabus.c), whose live
 * flags (DISPSTAT's, for one) are not the GBA's. openBus:
 * PORT_THUMB_OPEN_BUS(the opcode at the GBA load + 4) in fireemblem8.elf. The
 * value is zero-extended. This is the port's one sized GBA load (integration
 * 11 merged round 10's PortGbaLoadSized, of the traps and paths groups, into
 * it).
 */
static inline u32 PortGbaRead(u32 addr, u32 size, u32 openBus)
{
    if (size == 1)
        return PortGbaLoad8(addr, openBus, PortOpenBusLatch());
    if (size == 2)
        return PortGbaLoad16(addr, openBus, PortOpenBusLatch());
    return PortGbaLoad32(addr, openBus, PortOpenBusLatch());
}

#define PORT_GBA_READ_CHECK(size) ((void)sizeof(char[((size) == 1 || (size) == 2 || (size) == 4) ? 1 : -1]))

/* p->member as the GBA loads it (PortGbaRead), for any p. The address is
   computed as an integer, so a p that is no real object of its type (odd, or
   not aligned as the type) is never dereferenced as one. The zero-extended
   value is cast to the member's type, which gives LDRSH and LDRSB for a signed
   member at an aligned address only: at an odd address the ARM7's LDRSH loads
   the byte there sign-extended, and this gives the low 16 bits of the rotated
   halfword cast to the type (that byte zero-extended for an s16). No user has
   a signed member at an odd address; a new one must check. */
#define PORT_GBA_READ(p, member, openBus)                                                      \
    (PORT_GBA_READ_CHECK(sizeof((p)->member)),                                                 \
     (__typeof__((p)->member))PortGbaRead((u32)(uintptr_t)(p) + __builtin_offsetof(__typeof__(*(p)), member), \
                                          sizeof((p)->member), (openBus)))

/*
 * p->link->member, or what the GBA loads for it when p is NULL (a unit lookup
 * that found nothing, as GetUnit(0) for an empty square). The GBA loads link
 * through NULL, which gives the BIOS latch word (PORT_NULL_READ), then member
 * at that word + member's offset. For every latch word (0xE3A02004 post-SWI,
 * 0xE55EC002 post-IRQ, 0xE25EF004 inside an IRQ, 0xE129F000 at boot) plus a
 * small offset that address is unused space (0x10000000 and up), so the load
 * gets the CPU's open bus whatever the latch: openBus, the site's own
 * PORT_THUMB_OPEN_BUS(the opcode at the member load + 4) in fireemblem8.elf.
 * PORT_GBA_READ gives the GBA's bus at whatever address results, with the lane
 * at that address. When p is not NULL this is PORT_NULL_READ(p->link, member):
 * a NULL link reads the latch lanes, a live one the retail member.
 * (PORT_NULL_READ's compile-time check covers both loads: link and member are
 * 1-, 2- or 4-byte fields, aligned, below 0x4000.) p and p->link are evaluated
 * more than once: pass a plain variable (no side effects, as a local unit
 * pointer). Users: PidStatsRecordTargetListDefeats (src/bmtarget.c, working ID
 * D-60 (was working ID D-R10-TRAPS-1)) and the Devil Axe backfire on a wall, P-27 (src/bmmind.c,
 * D-73, was working ID D-R10-PATHS-1).
 */
#define PORT_NULL_READ_VIA(p, link, member, openBus)                                                     \
    ((p) != NULL ? PORT_NULL_READ((p)->link, member)                                                     \
                 : PORT_GBA_READ(PORT_NULL_READ(p, link), member, (openBus)))

/*
 * map[y][x] of a u8 map table (gBmMapUnit, gBmMapRange, ...: BmMapInit's row
 * pointers, src/bmmap.c) at any x, y, as the GBA loads it
 * (port-plan/DIVERGENCES.md D-50, was working ID D-MAPROW): the row word at map + 4 * y,
 * then the byte at row + x, with the GBA's 32-bit address arithmetic. On the
 * map and its edges (rows -2 to h+1, any x that stays in the pool) that is the
 * retail read of the same RAM. Past the row table (a unit outside a smaller
 * scene map, an undeployed or dead unit with x = -1 and an old y) the row word
 * comes from the table's tile area, and the byte from RAM, the BIOS latch (the
 * rule's, PortOpenBusLatch) or open bus (PortGbaLoad8, as D-27 does).
 * rowOpenBus and cellOpenBus are the open-bus words of the site's two loads:
 * PORT_THUMB_OPEN_BUS(the opcode at the load + 4) in fireemblem8.elf.
 */
static inline u8 PortBmMapRead(u8 **map, int x, int y, u32 rowOpenBus, u32 cellOpenBus)
{
    u32 latch = PortOpenBusLatch();
    u32 row = PortGbaLoad32((u32)(uintptr_t)map + 4 * (u32)y, rowOpenBus, latch);

    return (u8)PortGbaLoad8(row + (u32)x, cellOpenBus, latch);
}

/*
 * ((type *)proc->proc_parent)->member for a 4-byte member, as the GBA loads it
 * (port-plan/DIVERGENCES.md, D-66, was working ID D-R10-DEMONLIGHT-1). A proc started on
 * a proc tree root (Proc_Start(script, PROC_TREE_n)) gets the root's index n
 * (0-7) as its proc_parent (InsertRootProcess, src/proc.c), so the retail LDR
 * lands at n + the member's offset, in the BIOS area: the latch (the rule's,
 * PortOpenBusLatch), rotated right by 8 * ((n + offset) & 3) as the ARM7TDMI
 * rotates a misaligned word (PortGbaLoad32). A real parent is the retail load.
 * Words only: LDRH, LDRSH and LDRB at n + offset rotate or sign-extend by other
 * rules (an LDRSH at an odd address loads a sign-extended byte).
 */
#define PORT_PROC_PARENT_READ32(proc, type, member)                                                       \
    ((void)sizeof(char[sizeof(((type *)0)->member) == 4 && __builtin_offsetof(type, member) < 0x3FF8      \
                       ? 1 : -1]),                                                                        \
     (u32)(uintptr_t)(proc)->proc_parent < 8                                                              \
         ? (__typeof__(((type *)0)->member))PortGbaLoad32(                                                \
               (u32)(uintptr_t)(proc)->proc_parent + (u32)__builtin_offsetof(type, member),               \
               0 /* not used: the load is below 0x4000 */, PortOpenBusLatch())                            \
         : ((type *)(proc)->proc_parent)->member)

/*
 * map[y][x] = value, the store that goes with PortBmMapRead: the row word at
 * map + 4 * y with the GBA's 32-bit arithmetic, then the byte store at
 * row + x as the GBA bus takes it (PortGbaStore8). On the map and its edges
 * that is the retail store to the same RAM. Past the row table (a unit that
 * keeps its last square while a smaller map is loaded, or none: the Link
 * Arena's team set-up, port-plan/pending/folded/r10-gone.md,
 * D-69, was working ID D-R10-GONE-3) the row word comes from the table's tile area, 0 after
 * BmMapFill, so the store goes to address x: the BIOS area for x >= 0 and
 * 0xFFFFFFFF for x = -1, which both ignore it. For an s8 y the row word is
 * RAM (the table +-512 bytes), so the latch is never read and the result is
 * the same for every latch. rowOpenBus: PORT_THUMB_OPEN_BUS(the opcode at the
 * row load + 4) in fireemblem8.elf.
 */
static inline void PortBmMapWrite(u8 **map, int x, int y, u8 value, u32 rowOpenBus)
{
    u32 row = PortGbaLoad32((u32)(uintptr_t)map + 4 * (u32)y, rowOpenBus, PortOpenBusLatch());

    PortGbaStore8(row + (u32)x, value);
}

/*
 * Whether p is in the host's own image (its code, data and bss), not a GBA
 * address. Under D1-a the host links at 0x10000000 (Linux -Ttext-segment,
 * MinGW --image-base; port/win32/pe_gba.py), main_pc.c already treats
 * 0x10000000-0x1FFFFFFF as host code and data, and the GBA itself has nothing
 * there (open bus). For guards whose pointer is a host table on retail paths
 * but can also be a GBA address taken from GBA memory, possibly one the host
 * has no page for (port-plan/DIVERGENCES.md D-57, SetWorkingMoveCosts, and
 * D-55, GenUnitDefinitionFinalPosition): outside the image, load through
 * PortGbaLoad8/16/32.
 */
#define PORT_HOST_ADDRESS(p) ((u32)(uintptr_t)(p) - 0x10000000u < 0x10000000u)

/*
 * The host address of a GBA RAM address that can be on one of the RAM mirrors
 * (port-plan/DIVERGENCES.md, D-81, was working ID D-R10-FACES-2). The GBA decodes only
 * the low 18 address bits in 0x02000000-0x02FFFFFF (EWRAM, 256 KiB) and the
 * low 15 in 0x03000000-0x03FFFFFF (IWRAM, 32 KiB) (GBATEK "GBA Memory Map";
 * port/hw/gbabus.c's BusWord folds the same bits), and the host maps each RAM
 * once, at its GBA address (D1-a). Any other address is returned unchanged.
 * The game keeps its own value of the pointer (stored in its structs as the
 * GBA stores it); only the access goes through this. For guards whose game
 * code builds a RAM address from a junk index: StartPalFade's palette slot
 * from StartFaceFadeOut(NULL) (src/bmlib.c, src/face.c, working ID
 * D-80 (was working ID D-R10-FACES-1)). Pass only GBA addresses: on Windows the host's own heap can
 * lie in 0x02040000-0x03FFFFFF.
 */
static inline u32 PortGbaRamAddress(u32 addr)
{
    if ((addr >> 24) == 0x02)
        return 0x02000000u | (addr & 0x3FFFFu);
    if ((addr >> 24) == 0x03)
        return 0x03000000u | (addr & 0x7FFFu);
    return addr;
}

#define PORT_GBA_RAM(p) ((__typeof__(p))(uintptr_t)PortGbaRamAddress((u32)(uintptr_t)(p)))

/*
 * UNIT_CATTRIBUTES(unit) (bmunit.h: the character's attributes ORed with the
 * class's) as the GBA loads it when the unit's pCharacterData or pClassData is
 * NULL. attributes is the word at 0x28 of both structs, so the GBA ORs in the
 * whole BIOS latch word (PORT_NULL_READ, the rule's latch). Such a unit is a
 * thief who escaped (AiEscapeAction clears pCharacterData while the unit is
 * still gActiveUnit; HandlePostActionTraps, port-plan/pending/folded/r10-gone.md,
 * D-67, was working ID D-R10-GONE-1) or a unit killed in its own action (UnitKill;
 * CanUnitUseWeapon, D-37). With live pointers it is the retail macro.
 */
#define PORT_UNIT_CATTRIBUTES(unit) \
    (PORT_NULL_READ((unit)->pCharacterData, attributes) | PORT_NULL_READ((unit)->pClassData, attributes))

/*
 * UNIT_FACTION(unit) (bmunit.h: index & 0xC0) as the GBA loads it when unit is NULL (GetUnit(0)
 * for an obstacle's side of a battle, a one-actor map animation's empty actor). agbcc loads
 * index (+0x0B, s8) with LDRSB, so the GBA gets lane 3 of the BIOS latch word sign-extended
 * (PORT_NULL_READ, the rule's latch): 0xE3 post-SWI, 0xE5 post-IRQ, 0xE2 inside an IRQ, 0xE1
 * at boot, each & 0xC0 = 0xC0 (FACTION_PURPLE's bits: not blue, not red, not green) for every
 * latch. Users: BATTLE_HandleItemDrop's unitB (src/bmmind.c; port-plan/pending/folded/r10-nullunit2.md)
 * and the one-actor map animation (src/mapanim.c, D-22). With a live unit, the retail macro.
 */
#define PORT_UNIT_FACTION(unit) (PORT_NULL_READ(unit, index) & 0xC0)

/*
 * table[index] of a byte table in ROM data the host builds itself, as the GBA
 * loads it, for a site whose index can be junk (a class's terrain tables,
 * data_terrains.c, indexed by a terrain id read off the map:
 * SetBattleUnitTerrainBonusesAuto, port-plan/pending/folded/r10-gone.md,
 * D-68, was working ID D-R10-GONE-2). Inside the table (index < len) it is the host's own byte,
 * which is the GBA's: both builds compile the same data. Past its end the GBA
 * reads the ROM next to its own copy, and the host's copies have other
 * neighbours and gaps (GCC puts the 65-byte terrain tables 68 bytes apart,
 * agbcc 65), so the byte is loaded at the table's GBA address + index with
 * PortGbaLoad8: the ROM image, with the GBA's word where romreloc.c rewrote a
 * pointer, or open bus past it. The GBA address is the value the GBA ROM's own
 * pointers to the table hold (PortRomGbaAddressOf, port/hw/romreloc.c); a
 * table at a GBA address already (not PORT_HOST_ADDRESS) is loaded there; a
 * host table no ROM word points at (no caller has one) is read in place.
 * openBus: PORT_THUMB_OPEN_BUS(the opcode at the load + 4) in fireemblem8.elf.
 */
static inline u8 PortRomTableLoad8(const void *table, u32 index, u32 len, u32 openBus)
{
    u32 gba;

    if (index < len)
        return ((const u8 *)table)[index];
    gba = PORT_HOST_ADDRESS(table) ? PortRomGbaAddressOf(table) : (u32)(uintptr_t)table;
    if (gba == 0)
        return ((const u8 *)table)[index];
    return (u8)PortGbaLoad8(gba + index, openBus, PortOpenBusLatch());
}

/*
 * The size-byte (1, 2 or 4) load at byte offset off of a ROM table the host
 * builds itself, as the GBA makes it, for a site whose offset can run past
 * the table's end and whose table no ROM data word points at (so
 * PortRomGbaAddressOf has no address for it): gba is the table's address in
 * fireemblem8.elf, which the site's literal pool holds (round 12's fixes,
 * port-plan/DIVERGENCES.md, D-85: the world map's monster
 * tables, src/worldmap_timemons.c). Inside the table (off + size <= len) it
 * is the host's own bytes, which are the GBA's: both builds compile the same
 * data. Past its end it is PortGbaRead at gba + off: the ROM image as the GBA
 * has it, or open bus past it, never the host's neighbours (each linker puts
 * other data there). An aligned load inside a naturally aligned table only.
 * openBus: PORT_THUMB_OPEN_BUS(the opcode at the GBA load + 4).
 */
static inline u32 PortRomTableRead(const void *table, u32 gba, u32 off, u32 size, u32 len, u32 openBus)
{
    if (off + size <= len)
    {
        const u8 *p = (const u8 *)table + off;

        return size == 1 ? *p : size == 2 ? *(const u16 *)p : *(const u32 *)p;
    }
    return PortGbaRead(gba + off, size, openBus);
}

/*
 * Signed C division as the GBA computes it, for guarded sites whose divisor
 * can be 0 (the NULL probe lists such sites with how = "div0";
 * port-plan/DIVERGENCES.md D-46, the first such site; was working ID D-DIV0).
 * agbcc compiles `/` by a variable into a call of libgcc's
 * __divsi3 (fireemblem8.elf 0x080D18FC), which never traps: a zero divisor
 * branches to Ldiv0 (0x080D1984: bl __div0, which returns at once, then
 * r0 = 0), and otherwise the magnitudes are divided as unsigned numbers and the
 * quotient takes the sign of n ^ d, so INT_MIN / -1 is INT_MIN and every other
 * pair gives C's truncating quotient. The host's idiv raises #DE (SIGFPE) for
 * both special cases.
 */
static inline int PortDivsi3(int n, int d)
{
    if (d == 0)
        return 0;
    if (d == -1)
        return (int)(0u - (u32)n);
    return n / d;
}

/* port/hw/frame.c (P-25): takes the IRQs whose IF bit is latched, now that
   IME and IE allow them, as the GBA does at once after the store. */
void PortIrqTakePending(void);

#endif /* GUARD_PORT_PORT_H */
