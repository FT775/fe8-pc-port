#ifndef GUARD_PORT_WIN32_COMPAT_UNISTD_H
#define GUARD_PORT_WIN32_COMPAT_UNISTD_H

/*
 * Windows build only (see compat/time.h). MinGW's unistd.h has no fsync, so
 * gen_host_link.py would stub it silently (port/hw/platform_headless.c,
 * Platform_SaveFileAtomic). posix_win32.c implements it with _commit.
 */

#include_next <unistd.h>

int PortWin32Fsync(int fd);

#define fsync PortWin32Fsync

#endif /* GUARD_PORT_WIN32_COMPAT_UNISTD_H */
