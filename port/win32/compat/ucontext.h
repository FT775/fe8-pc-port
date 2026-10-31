#ifndef GUARD_PORT_WIN32_COMPAT_UCONTEXT_H
#define GUARD_PORT_WIN32_COMPAT_UCONTEXT_H

/*
 * Windows build only: the i386 glibc ucontext_t layout that port/hw/main_pc.c
 * reads in its fault handler (gregs[REG_EIP], gregs[REG_ERR]). posix_win32.c
 * fills it from the Windows CONTEXT and copies the registers back after the
 * handler returns, so a handler that moves EIP resumes there, as on Linux.
 *
 * REG_ERR follows the x86 page-fault error code: bit 1 = write, bit 2 = user
 * mode, bit 4 = instruction fetch (from EXCEPTION_RECORD.ExceptionInformation[0]).
 */

enum
{
    REG_GS, REG_FS, REG_ES, REG_DS, REG_EDI, REG_ESI, REG_EBP, REG_ESP, REG_EBX, REG_EDX,
    REG_ECX, REG_EAX, REG_TRAPNO, REG_ERR, REG_EIP, REG_CS, REG_EFL, REG_UESP, REG_SS,
    NGREG
};

typedef int greg_t;
typedef greg_t gregset_t[NGREG];

typedef struct
{
    gregset_t gregs;
} mcontext_t;

typedef struct ucontext_t
{
    unsigned long uc_flags;
    struct ucontext_t *uc_link;
    mcontext_t uc_mcontext;
} ucontext_t;

#endif /* GUARD_PORT_WIN32_COMPAT_UCONTEXT_H */
