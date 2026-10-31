#ifndef GUARD_PORT_WIN32_COMPAT_SYS_MMAN_H
#define GUARD_PORT_WIN32_COMPAT_SYS_MMAN_H

/*
 * Windows build only: the anonymous-mapping subset of mmap that
 * port/hw/memory.c uses to reserve the GBA regions at their GBA addresses.
 * posix_win32.c implements it with VirtualAlloc(MEM_RESERVE | MEM_COMMIT) at
 * the requested base. MAP_FIXED_NOREPLACE keeps its Linux meaning: the call
 * fails with EEXIST if anything already occupies the range, and never maps
 * somewhere else.
 */

#include <stddef.h>
#include <sys/types.h>

#define PROT_NONE  0
#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4

#define MAP_SHARED          0x01
#define MAP_PRIVATE         0x02
#define MAP_FIXED           0x10
#define MAP_ANONYMOUS       0x20
#define MAP_ANON            MAP_ANONYMOUS
#define MAP_FIXED_NOREPLACE 0x100000

#define MAP_FAILED ((void *)-1)

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int munmap(void *addr, size_t length);

#endif /* GUARD_PORT_WIN32_COMPAT_SYS_MMAN_H */
