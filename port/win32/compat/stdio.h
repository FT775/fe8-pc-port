#ifndef GUARD_PORT_WIN32_COMPAT_STDIO_H
#define GUARD_PORT_WIN32_COMPAT_STDIO_H

/*
 * Windows build only (see compat/time.h). msvcrt's rename fails when the
 * target exists, where POSIX replaces it; Platform_SaveFileAtomic
 * (port/hw/platform_headless.c) writes PATH.tmp and renames it over PATH.
 * posix_win32.c implements the POSIX behaviour with MoveFileExA
 * (MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH).
 */

#include_next <stdio.h>

int PortWin32Rename(const char *from, const char *to);

#define rename PortWin32Rename

#endif /* GUARD_PORT_WIN32_COMPAT_STDIO_H */
