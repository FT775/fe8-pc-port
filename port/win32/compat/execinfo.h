#ifndef GUARD_PORT_WIN32_COMPAT_EXECINFO_H
#define GUARD_PORT_WIN32_COMPAT_EXECINFO_H

/*
 * Windows build only: glibc's backtrace API, used by the fault report in
 * port/hw/main_pc.c. posix_win32.c implements it. Inside a fault handler it
 * starts from the faulting EIP and scans the faulting stack for return
 * addresses into fe8pc.exe (frame pointers are omitted at -O1). The image is
 * not relocatable, so the printed addresses work with
 * i686-w64-mingw32-addr2line -f -e build/win32/fe8pc.exe.
 */

int backtrace(void **buffer, int size);
void backtrace_symbols_fd(void *const *buffer, int size, int fd);

#endif /* GUARD_PORT_WIN32_COMPAT_EXECINFO_H */
