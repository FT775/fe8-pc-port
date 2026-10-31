#ifndef GUARD_PORT_WIN32_COMPAT_SIGNAL_H
#define GUARD_PORT_WIN32_COMPAT_SIGNAL_H

/*
 * Windows build only (port/Makefile.mingw puts port/win32/compat first on the
 * include path of port code). MinGW's <signal.h> has signal() but no
 * sigaction() or siginfo_t, which port/hw/main_pc.c uses for its fault
 * handler. This header adds the part main_pc.c needs; posix_win32.c
 * implements it with a vectored exception handler, so main_pc.c (the BIOS
 * write drop, the fault report) is compiled unchanged and behaves as on Linux.
 */

#include_next <signal.h>

#ifndef SIGBUS
#define SIGBUS 7 /* not in MinGW's set; the Linux i386 number */
#endif

#define SA_SIGINFO 4

typedef struct
{
    int si_signo;
    int si_errno;
    int si_code;
    void *si_addr; /* SIGSEGV/SIGBUS: the faulting data address; else the faulting EIP */
} siginfo_t;

struct sigaction
{
    void (*sa_handler)(int);
    void (*sa_sigaction)(int, siginfo_t *, void *);
    unsigned long sa_mask;
    int sa_flags;
};

int sigaction(int sig, const struct sigaction *act, struct sigaction *old);

/* glibc has it in <string.h>; MinGW has none. Same strings as glibc. */
char *strsignal(int sig);

#endif /* GUARD_PORT_WIN32_COMPAT_SIGNAL_H */
