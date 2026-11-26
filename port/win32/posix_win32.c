/*
 * Windows build only (port/Makefile.mingw). The two platform-specific pieces
 * of the D1-a host layer, so the shared port code builds unchanged:
 *
 *  - port/hw/memory.c reserves the GBA regions with mmap(MAP_FIXED_NOREPLACE).
 *    mmap() here does the same with VirtualAlloc at the fixed base.
 *  - port/hw/main_pc.c installs its fault handler with sigaction(SIGSEGV, ...)
 *    and reads the faulting EIP and the write bit from a ucontext_t. Here a
 *    vectored exception handler turns each fault into that call, and copies
 *    the registers back, so the handler's "skip the faulting store and resume"
 *    (the BIOS-area write drop, DIVERGENCES.md H-1) and its fault report work
 *    exactly as on Linux.
 *
 * Also the POSIX calls of port/hw/platform_headless.c that MinGW lacks or gets
 * from libraries this static build does not link (clock_gettime,
 * clock_nanosleep, fsync) and a replacing rename, through compat/time.h,
 * unistd.h and stdio.h.
 *
 * Plus three Windows conveniences: stdout in binary mode (logs end lines with
 * "\n" as on Linux), no Windows Error Reporting dialog on a crash (the log
 * has the fault report), and a ROM search next to the exe when no path is
 * given.
 *
 * And the save file's Windows pieces (port/save/savefile_pc.c): the file a
 * symbolic link names, the size and time of a file, whether two names are one
 * file, and the lock that keeps a second copy of the game from writing the
 * same save file.
 *
 * Both Windows exes link this file: the headless fe8pc.exe and the window
 * build fe8pc-sdl.exe (port/Makefile.mingw SDL=1). The window build compiles
 * it with FE8PC_WINGUI, which adds three calls into port/win32/gui/gui_win32.c
 * (the log file or --console of a GUI-subsystem exe, and the message boxes);
 * fe8pc.exe has none of them.
 *
 * Like all Windows objects, this file is built with -mno-ms-bitfields and
 * -mno-align-double (port/Makefile.mingw). The Win32 structs used here
 * (CONTEXT, EXCEPTION_RECORD, MEMORY_BASIC_INFORMATION, NT_TIB, the PE headers,
 * WIN32_FIND_DATAA, WIN32_FILE_ATTRIBUTE_DATA, BY_HANDLE_FILE_INFORMATION,
 * OVERLAPPED) have no bitfields
 * and no 64-bit members, so their layout is the same either way.
 */

#include <windows.h>

#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "execinfo.h"
#include "port_hw.h"
#include "signal.h"
#include "sys/mman.h"
#include "ucontext.h"
#include "port_state.h"

/* ------------------------------------------------------------------------- */
/* The exe image: the fault filter and the backtrace need its bounds.        */

static uint32_t sImageLo, sImageHi, sTextLo, sTextHi, sLinkStamp;
static char sExeName[64] = "fe8pc.exe"; /* the exe's file name, for the backtrace */

static void FindImage(void)
{
    const uint8_t *base;
    const IMAGE_NT_HEADERS32 *nt;
    const IMAGE_SECTION_HEADER *sec;
    unsigned i;

    if (sImageHi != 0)
        return;
    base = (const uint8_t *)GetModuleHandleA(NULL);
    nt = (const IMAGE_NT_HEADERS32 *)(base + ((const IMAGE_DOS_HEADER *)base)->e_lfanew);
    sImageLo = (uint32_t)(uintptr_t)base;
    sImageHi = sImageLo + nt->OptionalHeader.SizeOfImage;
    sLinkStamp = nt->FileHeader.TimeDateStamp;
    sec = IMAGE_FIRST_SECTION(nt);
    for (i = 0; i < nt->FileHeader.NumberOfSections; i++)
    {
        if (sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)
        {
            sTextLo = sImageLo + sec[i].VirtualAddress;
            sTextHi = sTextLo + sec[i].Misc.VirtualSize;
            break;
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Start-up                                                                  */

__attribute__((constructor)) static void PortWin32Init(void)
{
    char path[MAX_PATH * 4];
    DWORD n;
    const char *slash;

    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    /* Here, not in the fault handler: these take loader locks a fault may hold. */
    FindImage();
    n = GetModuleFileNameA(NULL, path, sizeof(path));
    if (n != 0 && n < sizeof(path) && (slash = strrchr(path, '\\')) != NULL && strlen(slash + 1) < sizeof(sExeName))
        strcpy(sExeName, slash + 1);
}

/* ------------------------------------------------------------------------- */
/* sigaction() on top of a vectored exception handler                        */

#define MAX_SIG 32

static struct sigaction sActions[MAX_SIG];
static int sHandlersInstalled;
static int sInDispatch;
static const CONTEXT *sFaultContext; /* for backtrace() inside a handler */

static int SignalFromException(DWORD code)
{
    switch (code)
    {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        return SIGSEGV;
    case EXCEPTION_DATATYPE_MISALIGNMENT:
    case EXCEPTION_IN_PAGE_ERROR:
        return SIGBUS;
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
        return SIGILL;
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_INT_OVERFLOW:
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
    case EXCEPTION_FLT_INVALID_OPERATION:
    case EXCEPTION_FLT_OVERFLOW:
    case EXCEPTION_FLT_UNDERFLOW:
    case EXCEPTION_FLT_INEXACT_RESULT:
    case EXCEPTION_FLT_DENORMAL_OPERAND:
    case EXCEPTION_FLT_STACK_CHECK:
        return SIGFPE;
    }
    return 0; /* breakpoints, stack overflow, guard pages, C++ and debug exceptions: not ours */
}

static int TrapNumber(int sig)
{
    switch (sig)
    {
    case SIGSEGV: return 14; /* #PF */
    case SIGBUS:  return 17; /* #AC */
    case SIGILL:  return 6;  /* #UD */
    case SIGFPE:  return 0;  /* #DE */
    }
    return 0;
}

static LONG Dispatch(int sig, EXCEPTION_POINTERS *ep)
{
    const EXCEPTION_RECORD *er = ep->ExceptionRecord;
    CONTEXT *c = ep->ContextRecord;
    struct sigaction *sa = &sActions[sig];
    siginfo_t si;
    ucontext_t uc;
    greg_t *g = uc.uc_mcontext.gregs;

    memset(&si, 0, sizeof(si));
    memset(&uc, 0, sizeof(uc));
    si.si_signo = sig;
    si.si_addr = er->ExceptionAddress;
    if ((er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION || er->ExceptionCode == EXCEPTION_IN_PAGE_ERROR) &&
        er->NumberParameters >= 2)
    {
        ULONG_PTR kind = er->ExceptionInformation[0]; /* 0 read, 1 write, 8 execute (DEP) */

        si.si_addr = (void *)er->ExceptionInformation[1];
        g[REG_ERR] = 4 | (kind == 1 ? 2 : 0) | (kind == 8 ? 0x10 : 0);
    }
    g[REG_GS] = (greg_t)c->SegGs;
    g[REG_FS] = (greg_t)c->SegFs;
    g[REG_ES] = (greg_t)c->SegEs;
    g[REG_DS] = (greg_t)c->SegDs;
    g[REG_EDI] = (greg_t)c->Edi;
    g[REG_ESI] = (greg_t)c->Esi;
    g[REG_EBP] = (greg_t)c->Ebp;
    g[REG_ESP] = (greg_t)c->Esp;
    g[REG_EBX] = (greg_t)c->Ebx;
    g[REG_EDX] = (greg_t)c->Edx;
    g[REG_ECX] = (greg_t)c->Ecx;
    g[REG_EAX] = (greg_t)c->Eax;
    g[REG_TRAPNO] = TrapNumber(sig);
    g[REG_EIP] = (greg_t)c->Eip;
    g[REG_CS] = (greg_t)c->SegCs;
    g[REG_EFL] = (greg_t)c->EFlags;
    g[REG_UESP] = (greg_t)c->Esp;
    g[REG_SS] = (greg_t)c->SegSs;

    sInDispatch = 1;
    sFaultContext = c;
    if (sa->sa_flags & SA_SIGINFO)
        sa->sa_sigaction(sig, &si, &uc);
    else
        sa->sa_handler(sig);
    sFaultContext = NULL;
    sInDispatch = 0;

    /* The handler returned: resume with the registers it left (Linux restores
       the ucontext the same way on return from the signal handler). */
    c->Edi = (DWORD)g[REG_EDI];
    c->Esi = (DWORD)g[REG_ESI];
    c->Ebp = (DWORD)g[REG_EBP];
    c->Esp = (DWORD)g[REG_ESP];
    c->Ebx = (DWORD)g[REG_EBX];
    c->Edx = (DWORD)g[REG_EDX];
    c->Ecx = (DWORD)g[REG_ECX];
    c->Eax = (DWORD)g[REG_EAX];
    c->Eip = (DWORD)g[REG_EIP];
    c->EFlags = (DWORD)g[REG_EFL];
    return EXCEPTION_CONTINUE_EXECUTION;
}

static int HasHandler(int sig)
{
    const struct sigaction *sa = &sActions[sig];

    return sig > 0 && sig < MAX_SIG &&
           ((sa->sa_flags & SA_SIGINFO) ? sa->sa_sigaction != NULL
                                        : (sa->sa_handler != NULL && sa->sa_handler != SIG_DFL &&
                                           sa->sa_handler != SIG_IGN));
}

/*
 * First chance: the game's faults. Those are faults in the exe, and the jumps
 * the game can make out of it: to NULL (the first 64 KiB, never mapped), into
 * one of the GBA regions memory.c has reserved (PortMemInGbaMap: EWRAM, IWRAM,
 * IO, PLTT, VRAM, OAM, ROM, SRAM, each at its GBA address, where no host code
 * is ever put), or to an address that is not mapped or not executable (the
 * fault address is EIP itself). Not the rest of 0x02000000-0x0E00FFFF: the
 * gaps between the regions (about 200 MB) are free for host memory, and
 * VirtualAlloc does hand out executable memory there (0x03850000 was seen).
 * A fault inside a DLL (a system DLL, SDL3.dll, a graphics driver) is left to
 * that DLL's own structured exception handling first, as Linux has no such
 * thing to model; so is a data fault in code outside every module that is not
 * the game's, such as a driver's generated code or an overlay's hook on SDL's
 * threads, in a gap or anywhere else, which may expect to handle its own
 * faults. If nobody handles one, the unhandled-exception filter below still
 * reports it.
 */
static LONG CALLBACK PortVectoredHandler(EXCEPTION_POINTERS *ep)
{
    const EXCEPTION_RECORD *er = ep->ExceptionRecord;
    int sig = SignalFromException(er->ExceptionCode);
    uint32_t eip = (uint32_t)ep->ContextRecord->Eip;
    HMODULE mod = NULL;

    if (sig == 0 || !HasHandler(sig) || sInDispatch)
        return EXCEPTION_CONTINUE_SEARCH;
    FindImage();
    if (eip >= sImageLo && eip < sImageHi)
        return Dispatch(sig, ep);
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCSTR)(uintptr_t)eip, &mod))
        return EXCEPTION_CONTINUE_SEARCH;
    if (eip < 0x10000u || PortMemInGbaMap(eip) ||
        (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2 &&
         (er->ExceptionInformation[0] == 8 || (uint32_t)er->ExceptionInformation[1] == eip)))
        return Dispatch(sig, ep);
    return EXCEPTION_CONTINUE_SEARCH;
}

static LONG WINAPI PortUnhandledFilter(EXCEPTION_POINTERS *ep)
{
    int sig = SignalFromException(ep->ExceptionRecord->ExceptionCode);

    if (sig == 0 || !HasHandler(sig) || sInDispatch)
        return EXCEPTION_CONTINUE_SEARCH;
    return Dispatch(sig, ep);
}

int sigaction(int sig, const struct sigaction *act, struct sigaction *old)
{
    if (sig <= 0 || sig >= MAX_SIG)
    {
        errno = EINVAL;
        return -1;
    }
    if (old != NULL)
        *old = sActions[sig];
    if (act != NULL)
    {
        sActions[sig] = *act;
        if (!sHandlersInstalled)
        {
            sHandlersInstalled = 1;
            AddVectoredExceptionHandler(1, PortVectoredHandler);
            SetUnhandledExceptionFilter(PortUnhandledFilter);
        }
    }
    return 0;
}

char *strsignal(int sig)
{
    static char unknown[32];

    switch (sig)
    {
    case SIGSEGV: return "Segmentation fault";
    case SIGBUS:  return "Bus error";
    case SIGILL:  return "Illegal instruction";
    case SIGFPE:  return "Floating point exception";
    case SIGABRT: return "Aborted";
    case SIGINT:  return "Interrupt";
    case SIGTERM: return "Terminated";
    }
    snprintf(unknown, sizeof(unknown), "Unknown signal %d", sig);
    return unknown;
}

/* ------------------------------------------------------------------------- */
/* backtrace()                                                               */

/* Is there a call instruction that returns to ret? (call rel32, call r/m32) */
static int FollowsCall(uint32_t ret)
{
    const uint8_t *p = (const uint8_t *)(uintptr_t)ret;

    if (ret - 7 < sTextLo || ret > sTextHi)
        return 0;
    if (p[-5] == 0xE8)
        return 1;                                       /* call rel32 */
    if (p[-2] == 0xFF && (p[-1] & 0xF8) == 0xD0)
        return 1;                                       /* call reg */
    if (p[-2] == 0xFF && (p[-1] & 0xF8) == 0x10 && (p[-1] & 7) != 4 && (p[-1] & 7) != 5)
        return 1;                                       /* call [reg] */
    if (p[-3] == 0xFF && (p[-2] & 0xF8) == 0x50)
        return 1;                                       /* call [reg+disp8] */
    if (p[-3] == 0xFF && p[-2] == 0x14)
        return 1;                                       /* call [sib] */
    if (p[-4] == 0xFF && p[-3] == 0x54)
        return 1;                                       /* call [sib+disp8] */
    if (p[-6] == 0xFF && ((p[-5] & 0xF8) == 0x90 || p[-5] == 0x15))
        return 1;                                       /* call [reg+disp32], call [abs32] */
    if (p[-7] == 0xFF && p[-6] == 0x94)
        return 1;                                       /* call [sib+disp32] */
    return 0;
}

int backtrace(void **buffer, int size)
{
    const NT_TIB *tib = (const NT_TIB *)NtCurrentTeb();
    const uint32_t *sp, *top;
    int n = 0;

    if (size <= 0)
        return 0;
    FindImage();
    if (sFaultContext == NULL)
        return (int)RtlCaptureStackBackTrace(0, (DWORD)size, buffer, NULL);

    /* In a fault handler: the faulting EIP, then return addresses found on
       the faulting stack (the code is built without frame pointers). */
    buffer[n++] = (void *)(uintptr_t)sFaultContext->Eip;
    sp = (const uint32_t *)(uintptr_t)sFaultContext->Esp;
    top = (const uint32_t *)tib->StackBase;
    for (; sp < top && n < size; sp++)
    {
        if (*sp >= sTextLo && *sp < sTextHi && FollowsCall(*sp))
            buffer[n++] = (void *)(uintptr_t)*sp;
    }
    return n;
}

/* The exe is at a fixed address (no relocations, no ASLR), so each address
   here is the one in the build's symbols. With the exe alone, nm -n EXE names
   the function (the last function symbol at or below the address: every
   Windows exe keeps its COFF symbols); addr2line -f -e EXE ADDR reads DWARF
   only, so it gives the function and source line where the debug information
   is: in the exe for test builds and the headless exe, and for the window
   build's release exe in fe8pc-sdl.debug, which addr2line finds beside the exe
   (or name it with -e; port/Makefile.mingw). The first line names the build:
   its link time stamp, which that .debug file's header carries too, the same
   value (objdump -p, "Time/Date"; port/Makefile.mingw, SPLIT_CMD). */
void backtrace_symbols_fd(void *const *buffer, int size, int fd)
{
    char line[160];
    int i, len;

    FindImage();
    len = snprintf(line, sizeof(line), "[fault] %s, link time stamp 0x%08X, image 0x%08X-0x%08X\n", sExeName,
        sLinkStamp, sImageLo, sImageHi);
    if (len > 0 && len < (int)sizeof(line))
        _write(fd, line, (unsigned)len);
    for (i = 0; i < size; i++)
    {
        uint32_t a = (uint32_t)(uintptr_t)buffer[i];

        if (a >= sImageLo && a < sImageHi)
            len = snprintf(line, sizeof(line), "%s(+0x%X) [0x%08X]\n", sExeName, a - sImageLo, a);
        else
            len = snprintf(line, sizeof(line), "[0x%08X]\n", a);
        if (len > 0 && len < (int)sizeof(line))
            _write(fd, line, (unsigned)len);
    }
}

/* ------------------------------------------------------------------------- */
/* mmap() for anonymous fixed mappings                                       */

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    DWORD protect;
    void *got;

    (void)offset;
    if (!(flags & MAP_ANONYMOUS) || fd != -1 || length == 0)
    {
        errno = EINVAL;
        return MAP_FAILED;
    }
    if (prot & PROT_EXEC)
        protect = (prot & PROT_WRITE) ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ;
    else if (prot & PROT_WRITE)
        protect = PAGE_READWRITE;
    else if (prot & PROT_READ)
        protect = PAGE_READONLY;
    else
        protect = PAGE_NOACCESS;

    got = VirtualAlloc(addr, length, MEM_RESERVE | MEM_COMMIT, protect);
    if (flags & (MAP_FIXED | MAP_FIXED_NOREPLACE))
    {
        /* Fixed: exactly there or not at all. Windows cannot replace an
           existing mapping, so MAP_FIXED behaves as MAP_FIXED_NOREPLACE. */
        if (got == addr)
            return got;
        if (got != NULL)
            VirtualFree(got, 0, MEM_RELEASE);
        else
        {
            MEMORY_BASIC_INFORMATION mbi;

            if ((uintptr_t)addr & 0xFFFF)
            {
                errno = EINVAL; /* VirtualAlloc reserves on 64 KB boundaries */
                return MAP_FAILED;
            }
            errno = (VirtualQuery(addr, &mbi, sizeof(mbi)) != 0 && mbi.State != MEM_FREE) ? EEXIST : ENOMEM;
            return MAP_FAILED;
        }
        errno = EEXIST;
        return MAP_FAILED;
    }
    if (got == NULL && addr != NULL)
        got = VirtualAlloc(NULL, length, MEM_RESERVE | MEM_COMMIT, protect); /* addr was a hint */
    if (got == NULL)
    {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    return got;
}

int munmap(void *addr, size_t length)
{
    (void)length;
    if (!VirtualFree(addr, 0, MEM_RELEASE))
    {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Clock, fsync and rename for port/hw/platform_headless.c                   */

/*
 * compat/time.h, unistd.h and stdio.h map clock_gettime, clock_nanosleep,
 * fsync and rename here. Without them MinGW's clock calls (libwinpthread, not
 * linked) and fsync (absent) became silent gen_host_link stubs, and msvcrt's
 * rename would not replace an existing save file.
 */
static LARGE_INTEGER freq; /* (file scope for save states: port_state.h, below) */
static HANDLE timer;

static int64_t NowNs(void)
{
    LARGE_INTEGER now;

    if (freq.QuadPart == 0)
        QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return (int64_t)(now.QuadPart / freq.QuadPart) * 1000000000 +
           (int64_t)(now.QuadPart % freq.QuadPart) * 1000000000 / freq.QuadPart;
}

int PortWin32ClockGetTime(int clock, struct timespec *ts)
{
    int64_t ns;

    if (clock != CLOCK_MONOTONIC)
    {
        errno = EINVAL;
        return -1;
    }
    ns = NowNs();
    ts->tv_sec = (time_t)(ns / 1000000000);
    ts->tv_nsec = (long)(ns % 1000000000);
    return 0;
}

/* CLOCK_MONOTONIC only. A waitable timer (high resolution where Windows has
   it) sleeps to about 1 ms before the deadline; the rest is a yielding wait. */
int PortWin32ClockNanosleep(int clock, int flags, const struct timespec *request, struct timespec *remain)
{
    int64_t deadline = (int64_t)request->tv_sec * 1000000000 + request->tv_nsec, left;

    (void)remain;
    if (clock != CLOCK_MONOTONIC)
        return EINVAL;
    if (!(flags & TIMER_ABSTIME))
        deadline += NowNs();
    if (timer == NULL)
    {
        timer = CreateWaitableTimerExW(NULL, NULL, 0x00000002 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */,
                                       TIMER_ALL_ACCESS);
        if (timer == NULL)
            timer = CreateWaitableTimerW(NULL, TRUE, NULL);
    }
    while ((left = deadline - NowNs()) > 0)
    {
        if (left > 2000000 && timer != NULL)
        {
            LARGE_INTEGER due;

            due.QuadPart = -(left - 1000000) / 100; /* relative, 100 ns units */
            if (SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE))
            {
                WaitForSingleObject(timer, INFINITE);
                continue;
            }
        }
        SwitchToThread();
    }
    return 0;
}

int PortWin32Fsync(int fd)
{
    return _commit(fd);
}

int PortWin32Rename(const char *from, const char *to)
{
    if (MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return 0;
    errno = EACCES;
    return -1;
}

/*
 * The file a save write replaces (port/save/savefile_pc.c, ResolveLink): a
 * save file that is a symbolic link (a reparse point) is written through it,
 * as mGBA does, so the temporary file and MoveFileEx, which would replace the
 * link itself, must use the file it names. 0: path is no link (out
 * untouched); 1: out is the final path of the file it names; -1 (errno set):
 * a link that names nothing that can be opened (a dangling link is not
 * followed here, unlike on Linux).
 */
int PortWin32FinalPath(const char *path, char *out, size_t outSize)
{
    char buf[MAX_PATH * 4];
    const char *p = buf;
    DWORD attr = GetFileAttributesA(path), n;
    HANDLE h;

    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_REPARSE_POINT))
        return 0;
    h = CreateFileA(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h == INVALID_HANDLE_VALUE)
    {
        errno = ENOENT;
        return -1;
    }
    n = GetFinalPathNameByHandleA(h, buf, sizeof(buf), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    CloseHandle(h);
    if (n == 0 || n >= sizeof(buf))
    {
        errno = ENAMETOOLONG;
        return -1;
    }
    /* "\\?\C:\dir\file" -> "C:\dir\file"; "\\?\UNC\server\share\..." -> "\\server\share\..." */
    if (strncmp(buf, "\\\\?\\UNC\\", 8) == 0)
    {
        buf[6] = '\\';
        p = buf + 6;
    }
    else if (strncmp(buf, "\\\\?\\", 4) == 0)
        p = buf + 4;
    if ((size_t)snprintf(out, outSize, "%s", p) >= outSize)
    {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 1;
}

static int ErrnoFromWin32(DWORD e)
{
    switch (e)
    {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
        return ENOENT;
    case ERROR_ACCESS_DENIED:
        return EACCES;
    case ERROR_WRITE_PROTECT:
        return EROFS;
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
        return EBUSY;
    case ERROR_FILENAME_EXCED_RANGE:
        return ENAMETOOLONG;
    case ERROR_NOT_SUPPORTED:
    case ERROR_INVALID_FUNCTION:
        return ENOSYS;
    }
    return EIO;
}

/*
 * The save file's size and last-write time (port/save/savefile_pc.c,
 * StatFile), for the check before each write: GetFileAttributesEx, since
 * MinGW's stat is _stat32, which gen_host_link.py would stub. mtime in 100 ns
 * units. 0; 1 when there is no file; -1 (errno set) when it cannot be looked
 * at.
 */
int PortWin32FileStamp(const char *path, uint64_t *size, int64_t *mtime)
{
    WIN32_FILE_ATTRIBUTE_DATA d;
    DWORD e;

    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &d))
    {
        e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND)
            return 1;
        errno = ErrnoFromWin32(e);
        return -1;
    }
    *size = (uint64_t)d.nFileSizeHigh << 32 | d.nFileSizeLow;
    *mtime = (int64_t)((uint64_t)d.ftLastWriteTime.dwHighDateTime << 32 | d.ftLastWriteTime.dwLowDateTime);
    return 0;
}

/*
 * Whether two paths name one file (port/save/savefile_pc.c, SameFile): the
 * volume serial number and the file index, Windows' device and inode numbers,
 * so a path spelt another way (other letter case, / for \, a relative path, a
 * symbolic link, a hard link) is still the same file. 1: one file; 0: two
 * files; -1 (errno set): one of them cannot be opened.
 */
static int FileId(const char *path, BY_HANDLE_FILE_INFORMATION *info)
{
    HANDLE h = CreateFileA(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS, NULL);
    BOOL ok;

    if (h == INVALID_HANDLE_VALUE)
    {
        errno = ErrnoFromWin32(GetLastError());
        return -1;
    }
    ok = GetFileInformationByHandle(h, info);
    if (!ok)
        errno = ErrnoFromWin32(GetLastError());
    CloseHandle(h);
    return ok ? 0 : -1;
}

int PortWin32SameFile(const char *a, const char *b)
{
    BY_HANDLE_FILE_INFORMATION ia, ib;

    if (FileId(a, &ia) != 0 || FileId(b, &ib) != 0)
        return -1;
    return ia.dwVolumeSerialNumber == ib.dwVolumeSerialNumber && ia.nFileIndexHigh == ib.nFileIndexHigh &&
           ia.nFileIndexLow == ib.nFileIndexLow;
}

/*
 * The save file's lock (port/save/savefile_pc.c, TryLock): an exclusive
 * LockFileEx on the lock file PATH (made when it is not there), on a handle
 * kept open for the rest of the run, so the lock lasts until the process ends
 * and Windows lets it go then. Other processes may open the file (the share
 * flags), so a second copy gets ERROR_LOCK_VIOLATION, not a sharing error.
 * 1: locked; 0: another process holds it; -1 (errno set): no lock (the file
 * cannot be opened or made, or the file system has no locks).
 */
int PortWin32LockFile(const char *path)
{
    const DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    HANDLE h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, share, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    OVERLAPPED ov;
    DWORD e;

    /* A lock file with the read-only attribute: LockFileEx needs only read access. */
    if (h == INVALID_HANDLE_VALUE && GetLastError() == ERROR_ACCESS_DENIED)
        h = CreateFileA(path, GENERIC_READ, share, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
    {
        e = GetLastError();
        if (e == ERROR_SHARING_VIOLATION)
            return 0;
        errno = ErrnoFromWin32(e);
        return -1;
    }
    memset(&ov, 0, sizeof(ov));
    if (LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &ov))
        return 1; /* h stays open: closing it would let the lock go */
    e = GetLastError();
    CloseHandle(h);
    if (e == ERROR_LOCK_VIOLATION || e == ERROR_SHARING_VIOLATION || e == ERROR_IO_PENDING)
        return 0;
    errno = ErrnoFromWin32(e);
    return -1;
}

/*
 * The shell's Roaming application data folder (port/sdl3/platform_sdl3.c,
 * Platform_UserDir), in UTF-8, as SDL_GetPrefPath finds it:
 * SHGetFolderPathW(CSIDL_APPDATA), from shell32 by name, since the exe may
 * import only KERNEL32 and msvcrt (port/Makefile.mingw; called after the GBA
 * map is reserved). %APPDATA% when shell32 cannot say. 0, or -1.
 */
int PortWin32RoamingDir(char *out, size_t outSize)
{
    typedef HRESULT(WINAPI * GetFolderPathFn)(HWND, int, HANDLE, DWORD, LPWSTR);
    wchar_t path[MAX_PATH + 1];
    HMODULE shell32 = LoadLibraryW(L"shell32.dll");
    GetFolderPathFn getFolderPath =
        shell32 != NULL ? (GetFolderPathFn)(void *)GetProcAddress(shell32, "SHGetFolderPathW") : NULL;
    const wchar_t *env;

    if (getFolderPath == NULL || getFolderPath(NULL, 0x001A /* CSIDL_APPDATA */, NULL, 0, path) != S_OK)
    {
        env = _wgetenv(L"APPDATA");
        if (env == NULL || env[0] == L'\0' || wcslen(env) >= sizeof(path) / sizeof(path[0]))
            return -1;
        wcscpy(path, env);
    }
    if (WideCharToMultiByte(CP_UTF8, 0, path, -1, out, (int)outSize, NULL, NULL) == 0)
        return -1;
    return 0;
}

/* ------------------------------------------------------------------------- */
/* The ROM next to the exe                                                   */

/*
 * port/hw/main_pc.c reads the ROM from its command line (PortParseArgs: the
 * one argument that is not a save option), else "baserom.gba" in the current
 * directory. Linked with --wrap=main: when neither is there, look next
 * to fe8pc.exe, so a double-clicked exe finds the user's ROM: the original
 * FE8 (USA) ROM (memory.c's checks: header, size and SHA-1), baserom.gba
 * first, then any .gba file; when there is none, the first file with the
 * FE8 (USA) header (baserom.gba first), so memory.c says what is wrong with
 * it (a dump cut short, a changed ROM) rather than "no ROM found".
 *
 * v1.22.0: then the "roms" folder next to the exe (the release zip has one, with
 * "PUT YOUR ROMS HERE.txt"), by the same rules. The order: the original ROM next
 * to the exe, then the original ROM in roms\, then a file with only the FE8
 * header next to the exe, then one in roms\. Every place looked at before
 * v1.22.0 comes first, so a setup that started the game then still picks the
 * same ROM; only a setup that found no original ROM can find one in roms\ (a
 * hack next to the exe used to stop with an error). The window build then asks
 * (port/win32/gui/gui_win32.c PortWinGui_FindRom: the path remembered from an
 * earlier choice, else an Open dialog) whenever no original ROM was found: the
 * original ROM by SHA-1 wins over a file with only the FE8 header wherever it
 * is, and that file is used (memory.c then says what is wrong with it) only
 * when there is none (Cancel, or nobody to ask). The save, window build: for a
 * ROM found in roms\ or chosen (or remembered), exactly as if that ROM file sat
 * next to the exe: same file name, the exe's folder, savefile_pc.c's per-user
 * rules (SaveAsNextToExe). Every other ROM (next to the exe, baserom.gba, given
 * on the command line or dropped onto the exe) keeps v1.21.0's save, the one
 * next to it; the headless build keeps a save only when its command line says
 * --save, as before.
 *
 * The window build (FE8PC_WINGUI) also sets up its log first (a log file, or
 * the console with --console, which it takes off the command line), says in
 * a message box when no ROM is found, and when the game stops before it
 * starts (port/win32/gui/gui_win32.c). main() returns only then: once the
 * game runs, it ends through PortExit's exit() or a fault's _exit().
 */
int __real_main(int argc, char **argv);

#ifdef FE8PC_WINGUI
/* port/win32/gui/gui_win32.c */
void PortWinGui_Start(int *argc, char ***argv);
int PortWinGui_NoRom(void);
void PortWinGui_StartFailed(int code);
int PortWinGui_FindRom(char *out, size_t outSize, const char *notOriginal);
#endif

/* The exe's folder (no last backslash); 0 when it is not known. */
static int ExeDirA(char *dir, size_t dirSize)
{
    DWORD n = GetModuleFileNameA(NULL, dir, (DWORD)dirSize);
    char *slash;

    if (n == 0 || n >= dirSize)
        return 0;
    slash = strrchr(dir, '\\');
    if (slash == NULL)
        return 0;
    *slash = '\0';
    return 1;
}

/* Save states (port/reforged/reforged_savestate.c, port_hw.h): this exe's path, to take its build id. */
int PortExePath(char *out, size_t outSize)
{
    DWORD n = GetModuleFileNameA(NULL, out, (DWORD)outSize);

    return n != 0 && n < outSize;
}

/* In dir: 1: out is the original FE8 (USA) ROM (baserom.gba first, then any
   .gba file); else fallback gets the first file with only the FE8 (USA)
   header, when it is still empty; 0. */
static int FindRomIn(const char *dir, char *out, size_t outSize, char *fallback, size_t fallbackSize)
{
    char pattern[MAX_PATH * 4 + 8]; /* UTF-8 paths in the window build (its manifest) */
    char sha1[41];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int kind;

    snprintf(out, outSize, "%s\\baserom.gba", dir);
    kind = PortRomFileKind(out, sha1);
    if (kind == PORT_ROM_FE8)
        return 1;
    if (kind != PORT_ROM_NOT_FE8 && fallback[0] == '\0')
        snprintf(fallback, fallbackSize, "%s", out);
    snprintf(pattern, sizeof(pattern), "%s\\*.gba", dir);
    h = FindFirstFileA(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            snprintf(out, outSize, "%s\\%s", dir, fd.cFileName);
            kind = PortRomFileKind(out, sha1);
            if (kind == PORT_ROM_FE8)
            {
                FindClose(h);
                return 1;
            }
            if (kind != PORT_ROM_NOT_FE8 && fallback[0] == '\0')
                snprintf(fallback, fallbackSize, "%s", out);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    return 0;
}

/* 1: out is the original FE8 (USA) ROM next to the exe, or (v1.22.0) in its
   roms folder; 2: out has only the FE8 (USA) header (the best there is; next
   to the exe first); 0: none. *where: "next to the exe" or "in the roms folder". */
static int FindRomNextToExe(char *out, size_t outSize, const char **where)
{
    static char fallback[MAX_PATH * 8], fallbackRoms[MAX_PATH * 8];
    char dir[MAX_PATH * 4], roms[MAX_PATH * 4 + 8];

    if (!ExeDirA(dir, sizeof(dir)))
        return 0;
    snprintf(roms, sizeof(roms), "%s\\roms", dir);
    fallback[0] = fallbackRoms[0] = '\0';
    *where = "next to the exe";
    if (FindRomIn(dir, out, outSize, fallback, sizeof(fallback)))
        return 1;
    *where = "in the roms folder";
    if (FindRomIn(roms, out, outSize, fallbackRoms, sizeof(fallbackRoms)))
        return 1;
    if (fallback[0] == '\0' && fallbackRoms[0] == '\0')
        return 0;
    *where = fallback[0] != '\0' ? "next to the exe" : "in the roms folder";
    snprintf(out, outSize, "%s", fallback[0] != '\0' ? fallback : fallbackRoms);
    return 2;
}

#ifdef FE8PC_WINGUI
/* v1.22.0: for a ROM found in the roms folder or chosen with the picker, the save is chosen as for a ROM of
   that file name next to the exe (savefile_pc.c PortSave_SetSaveBase): the save stays next to the game. */
static void SaveAsNextToExe(const char *romPath)
{
    static char base[MAX_PATH * 12 + 8];
    char dir[MAX_PATH * 4];
    const char *name = strrchr(romPath, '\\'), *slash2 = strrchr(romPath, '/');

    if (slash2 != NULL && (name == NULL || slash2 > name))
        name = slash2;
    name = name != NULL ? name + 1 : romPath;
    if (!ExeDirA(dir, sizeof(dir)))
        return;
    snprintf(base, sizeof(base), "%s\\%s", dir, name);
    PortSave_SetSaveBase(base);
    printf("[win32] the save is kept as for '%s' (next to the exe, where it always was)\n", base);
    fflush(stdout);
}
#endif

int __wrap_main(int argc, char **argv)
{
    static char romPath[MAX_PATH * 8];
    struct PortArgs args;
    const char *error, *where = "";
    char **newArgv;
    int i, rc, found;

#ifdef FE8PC_WINGUI
    PortWinGui_Start(&argc, &argv);
#endif
    /* No ROM among the arguments (the save options may be there: main_pc.c's
       own parser says): the ROM found next to the exe or in its roms folder (or,
       in the window build, the one remembered or chosen) goes last. */
    if (PortParseArgs(argc, argv, &args, &error) == 0 && args.rom == NULL &&
        GetFileAttributesA("baserom.gba") == INVALID_FILE_ATTRIBUTES)
    {
        found = FindRomNextToExe(romPath, sizeof(romPath), &where);
#ifdef FE8PC_WINGUI
        if (found != 1)
        {
            /* no original ROM there: the one remembered or chosen; a file found with only the FE8 header (a
               hack, a cut-short copy) is used only without one (Cancel), so memory.c says what is wrong with
               it, as before v1.22.0 (it never started the game) */
            static char headerOnly[MAX_PATH * 8];

            snprintf(headerOnly, sizeof(headerOnly), "%s", found == 2 ? romPath : "");
            if (PortWinGui_FindRom(romPath, sizeof(romPath), found == 2 ? headerOnly : NULL))
            {
                found = 1;
                where = "chosen in the Open dialog (or remembered from that choice)";
            }
            else if (found == 2)
                snprintf(romPath, sizeof(romPath), "%s", headerOnly);
        }
#endif
        if (found != 0 && (newArgv = calloc((size_t)argc + 2, sizeof(*newArgv))) != NULL)
        {
            printf("[win32] no ROM path given and no baserom.gba here: using '%s' (%s)%s\n", romPath, where,
                found == 2 ? ": it has the FE8 (USA) header, but no file there is the original ROM (see below)" : "");
            fflush(stdout);
#ifdef FE8PC_WINGUI
            if (strcmp(where, "next to the exe") != 0) /* in roms\, or chosen */
                SaveAsNextToExe(romPath);
#endif
            for (i = 0; i < argc; i++)
                newArgv[i] = argv[i];
            newArgv[argc] = romPath;
            newArgv[argc + 1] = NULL;
            argc++;
            argv = newArgv;
        }
#ifdef FE8PC_WINGUI
        else
            return PortWinGui_NoRom();
#endif
    }
    rc = __real_main(argc, argv);
#ifdef FE8PC_WINGUI
    PortWinGui_StartFailed(rc);
#endif
    return rc;
}

/* v1.31.0 (platform_sdl3.c Platform_HttpsGet): the System32 folder (no trailing backslash); 0, or -1. */
int PortWin32SystemDir(char *out, size_t outSize)
{
    UINT n = GetSystemDirectoryA(out, (UINT)outSize);

    return n == 0 || n >= outSize ? -1 : 0;
}

/* v1.31.0 (platform_sdl3.c Platform_HttpsGet): runs cmdline with no window, its standard output into out
   (NUL-terminated, at most outSize - 1 bytes); 1 when it ran, exited 0 and wrote something. Waits at most
   15 s for the exit after the output ends (curl ends itself after --max-time). */
int PortWin32RunHidden(const char *cmdline, char *out, size_t outSize)
{
    SECURITY_ATTRIBUTES sa;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    HANDLE rd = NULL, wr = NULL;
    char cmd[1024];
    DWORD got = 0, total = 0, code = 1;
    int ok = 0;

    if (out == NULL || outSize == 0)
        return 0;
    out[0] = '\0';
    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    if (strlen(cmdline) >= sizeof(cmd) || !CreatePipe(&rd, &wr, &sa, 0))
        return 0;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = wr;
    si.hStdError = NULL;
    si.hStdInput = NULL;
    strcpy(cmd, cmdline);
    if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
    {
        CloseHandle(rd);
        CloseHandle(wr);
        return 0;
    }
    CloseHandle(wr);
    while (total + 1 < outSize && ReadFile(rd, out + total, (DWORD)(outSize - 1 - total), &got, NULL) && got > 0)
        total += got;
    out[total] = '\0';
    if (WaitForSingleObject(pi.hProcess, 15000) == WAIT_OBJECT_0 && GetExitCodeProcess(pi.hProcess, &code))
        ok = code == 0 && total > 0;
    else
        TerminateProcess(pi.hProcess, 1);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(rd);
    return ok;
}

/* --- v1.33.0, the updater (platform_sdl3.c; port/reforged/reforged_update.c) --- */

/* This exe's path and its folder (with the trailing backslash). */
static int ExeAndDir(char *exe, size_t exeSize, char *dir, size_t dirSize)
{
    DWORD n = GetModuleFileNameA(NULL, exe, (DWORD)exeSize);
    char *slash;

    if (n == 0 || n >= exeSize)
        return -1;
    slash = strrchr(exe, '\\');
    if (slash == NULL || (size_t)(slash - exe + 2) > dirSize)
        return -1;
    memcpy(dir, exe, (size_t)(slash - exe + 1));
    dir[slash - exe + 1] = '\0';
    return 0;
}

/* %TEMP%\fe8pc-update\, emptied (only the names this updater writes there). */
int PortWin32UpdateTempDir(char *out, size_t outSize)
{
    char tmp[MAX_PATH], path[MAX_PATH * 2];
    DWORD n = GetTempPathA(sizeof(tmp), tmp);

    if (n == 0 || n >= sizeof(tmp) || (size_t)snprintf(out, outSize, "%sfe8pc-update\\", tmp) >= outSize)
        return -1;
    CreateDirectoryA(out, NULL);
    snprintf(path, sizeof(path), "%supdate.zip", out);
    DeleteFileA(path);
    return GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES ? 0 : -1;
}

/* Runs cmdline with no window; its standard output into a growing malloc'd buffer (*out, *outLen; at most
   maxLen bytes, else a failure), its exit code into *code. 0 when it ran to its end. */
static int RunHiddenAll(const char *cmdline, unsigned char **out, size_t *outLen, size_t maxLen, DWORD *code)
{
    SECURITY_ATTRIBUTES sa;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    HANDLE rd = NULL, wr = NULL;
    char cmd[1024];
    unsigned char *buf = NULL;
    size_t cap = 0, len = 0;
    DWORD got = 0;
    int ok = -1;

    *out = NULL, *outLen = 0, *code = 1;
    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    if (strlen(cmdline) >= sizeof(cmd) || !CreatePipe(&rd, &wr, &sa, 0))
        return -1;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = wr;
    strcpy(cmd, cmdline);
    if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
    {
        CloseHandle(rd);
        CloseHandle(wr);
        return -1;
    }
    CloseHandle(wr);
    for (;;)
    {
        if (len == cap)
        {
            unsigned char *nb;

            if (cap >= maxLen)
                break; /* too much: a failure below */
            cap = cap == 0 ? 1u << 20 : cap * 2;
            if (cap > maxLen)
                cap = maxLen;
            nb = realloc(buf, cap);
            if (nb == NULL)
                break;
            buf = nb;
        }
        if (!ReadFile(rd, buf + len, (DWORD)(cap - len), &got, NULL) || got == 0)
        {
            ok = 0;
            break;
        }
        len += got;
    }
    CloseHandle(rd);
    if (WaitForSingleObject(pi.hProcess, 15000) != WAIT_OBJECT_0 || !GetExitCodeProcess(pi.hProcess, code))
    {
        TerminateProcess(pi.hProcess, 1);
        ok = -1;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (ok != 0)
    {
        free(buf);
        return -1;
    }
    *out = buf, *outLen = len;
    return 0;
}

/* data into path (created or replaced), every byte written and flushed; 0, or -1 (path deleted). */
static int WriteWhole(const char *path, const unsigned char *data, size_t len)
{
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    size_t done = 0;
    int ok;

    if (h == INVALID_HANDLE_VALUE)
        return -1;
    while (done < len)
    {
        DWORD w = 0, n = (DWORD)(len - done > (1u << 24) ? (1u << 24) : len - done);

        if (!WriteFile(h, data + done, n, &w, NULL) || w != n)
            break;
        done += w;
    }
    ok = done == len && FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok)
        DeleteFileA(path);
    return ok ? 0 : -1;
}

/* One member of the (SHA-256-checked) zip, as Windows' tar gives it on its standard output (tar checks the
   member's CRC and fails on a bad one), written to dst; at least minSize bytes. 0, or -1 (dst not there). */
static int ExtractTo(const char *sys, const char *zipPath, const char *member, const char *dst, size_t minSize)
{
    char cmd[1024];
    unsigned char *data;
    size_t len;
    DWORD code;
    int r;

    if ((size_t)snprintf(cmd, sizeof(cmd), "\"%s\\tar.exe\" -xOf \"%s\" \"%s\"", sys, zipPath, member) >= sizeof(cmd))
        return -1;
    if (RunHiddenAll(cmd, &data, &len, 64u << 20, &code) != 0)
    {
        PortLog("[update] tar could not give %s\n", member);
        return -1;
    }
    if (code != 0 || len < minSize)
    {
        PortLog("[update] tar gave %s with exit %lu, %lu bytes: not used\n", member, (unsigned long)code, (unsigned long)len);
        free(data);
        return -1;
    }
    r = WriteWhole(dst, data, len);
    free(data);
    if (r != 0)
        PortLog("[update] cannot write %s (error %lu)\n", dst, (unsigned long)GetLastError());
    return r;
}

/* The new fe8pc-sdl.exe and SDL3.dll from the zip's folder: first written beside the game as .new (a failure
   there changes nothing), then four renames on the same drive (cur -> .old, .new -> cur), each checked and
   undone on a failure. 0 when the new files are in place, -1 when the old ones still are. tmpDir: unused. */
int PortWin32InstallUpdate(const char *zipPath, const char *tmpDir, const char *folder)
{
    char exe[MAX_PATH], dir[MAX_PATH], sys[MAX_PATH], member[160];
    char dll[MAX_PATH + 16], exeNew[MAX_PATH + 16], dllNew[MAX_PATH + 32], exeOld[MAX_PATH + 16], dllOld[MAX_PATH + 32];

    (void)tmpDir;
    if (ExeAndDir(exe, sizeof(exe), dir, sizeof(dir)) != 0 || PortWin32SystemDir(sys, sizeof(sys)) != 0 ||
        strchr(folder, '\\') != NULL || strchr(folder, '/') != NULL || strstr(folder, "..") != NULL ||
        strchr(folder, '"') != NULL || strlen(folder) > 100)
        return -1;
    snprintf(dll, sizeof(dll), "%sSDL3.dll", dir);
    snprintf(exeNew, sizeof(exeNew), "%s.new", exe);
    snprintf(dllNew, sizeof(dllNew), "%s.new", dll);
    snprintf(exeOld, sizeof(exeOld), "%s.old", exe);
    snprintf(dllOld, sizeof(dllOld), "%s.old", dll);
    snprintf(member, sizeof(member), "%s/fe8pc-sdl.exe", folder);
    if (ExtractTo(sys, zipPath, member, exeNew, 1000000u) != 0)
        return -1;
    snprintf(member, sizeof(member), "%s/SDL3.dll", folder);
    if (ExtractTo(sys, zipPath, member, dllNew, 100000u) != 0)
    {
        DeleteFileA(exeNew);
        return -1;
    }
    DeleteFileA(exeOld);
    DeleteFileA(dllOld);
    /* a running exe and its loaded DLL can be renamed, not overwritten */
    if (!MoveFileA(exe, exeOld))
    {
        PortLog("[update] cannot rename %s (error %lu)\n", exe, (unsigned long)GetLastError());
        goto undo0;
    }
    if (!MoveFileA(dll, dllOld))
    {
        PortLog("[update] cannot rename %s (error %lu)\n", dll, (unsigned long)GetLastError());
        goto undo1;
    }
    if (!MoveFileA(exeNew, exe))
    {
        PortLog("[update] cannot rename %s (error %lu)\n", exeNew, (unsigned long)GetLastError());
        goto undo2;
    }
    if (!MoveFileA(dllNew, dll))
    {
        PortLog("[update] cannot rename %s (error %lu)\n", dllNew, (unsigned long)GetLastError());
        if (!MoveFileExA(exe, exeNew, MOVEFILE_REPLACE_EXISTING))
            PortLog("[update] cannot move the new exe aside (error %lu)\n", (unsigned long)GetLastError());
        goto undo2;
    }
    PortLog("[update] %s and SDL3.dll replaced (the old ones kept as .old until the next start)\n", exe);
    return 0;
undo2:
    if (!MoveFileExA(dllOld, dll, MOVEFILE_REPLACE_EXISTING))
        PortLog("[update] PUT BACK FAILED: rename %s to SDL3.dll yourself (error %lu)\n", dllOld, (unsigned long)GetLastError());
undo1:
    if (!MoveFileExA(exeOld, exe, MOVEFILE_REPLACE_EXISTING))
        PortLog("[update] PUT BACK FAILED: rename %s to fe8pc-sdl.exe yourself (error %lu)\n", exeOld, (unsigned long)GetLastError());
undo0:
    DeleteFileA(exeNew);
    DeleteFileA(dllNew);
    return -1;
}

/* This exe again, with this run's command line; FE8PC_UPDATED=<this process's id> in its environment, so it
   waits for this one to have ended (its save file's lock released) before it opens the save
   (PortWin32WaitForUpdater). */
void PortWin32RestartGame(void)
{
    char exe[MAX_PATH], dir[MAX_PATH], pid[16], *line = GetCommandLineA();
    static char cmd[8192];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;

    if (ExeAndDir(exe, sizeof(exe), dir, sizeof(dir)) != 0 || strlen(line) >= sizeof(cmd))
        return;
    strcpy(cmd, line);
    snprintf(pid, sizeof(pid), "%lu", (unsigned long)GetCurrentProcessId());
    SetEnvironmentVariableA("FE8PC_UPDATED", pid);
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    if (CreateProcessA(exe, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    else
        PortLog("[update] cannot start %s again (error %lu): start it yourself\n", exe, (unsigned long)GetLastError());
}

/* port/hw/main_pc.c, before the save file is opened: started by an install (FE8PC_UPDATED=<pid>), wait (at most
   20 s) for the game that installed this one to end, so its save file's lock is free. */
void PortWin32WaitForUpdater(void)
{
    const char *s = getenv("FE8PC_UPDATED");
    unsigned long pid = s != NULL ? strtoul(s, NULL, 10) : 0;
    HANDLE h;

    if (pid == 0 || pid == GetCurrentProcessId())
        return;
    h = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid);
    if (h == NULL)
        return; /* already gone */
    if (WaitForSingleObject(h, 20000) != WAIT_OBJECT_0)
        PortLog("[update] the game that installed this one has not ended after 20 s\n");
    CloseHandle(h);
}

/* What an install left: the .old files (once nothing runs from them) and any .new ones. */
void PortWin32UpdateCleanup(void)
{
    static const char *const kSuffix[] = { "fe8pc-sdl.exe.old", "SDL3.dll.old", "fe8pc-sdl.exe.new", "SDL3.dll.new" };
    char exe[MAX_PATH], dir[MAX_PATH], path[MAX_PATH * 2];
    const char *base;
    size_t i;

    if (ExeAndDir(exe, sizeof(exe), dir, sizeof(dir)) != 0)
        return;
    base = exe + strlen(dir);
    for (i = 0; i < sizeof(kSuffix) / sizeof(kSuffix[0]); i++)
    {
        /* the exe's own name may differ from fe8pc-sdl.exe: its .old / .new go by its name */
        if (i == 0 || i == 2)
            snprintf(path, sizeof(path), "%s%s%s", dir, base, i == 0 ? ".old" : ".new");
        else
            snprintf(path, sizeof(path), "%s%s", dir, kSuffix[i]);
        if (DeleteFileA(path))
            PortLog("[update] deleted %s\n", path);
    }
}

/* Save states (port/include/port_state.h): not game state: the exe image's bounds, the signal emulation,
   the clock's frequency and the sleep timer (a handle of this process). */
PORT_STATE_SESSION(sImageLo)
PORT_STATE_SESSION(sImageHi)
PORT_STATE_SESSION(sTextLo)
PORT_STATE_SESSION(sTextHi)
PORT_STATE_SESSION(sLinkStamp)
PORT_STATE_SESSION(sExeName)
PORT_STATE_SESSION(sActions)
PORT_STATE_SESSION(sHandlersInstalled)
PORT_STATE_SESSION(sInDispatch)
PORT_STATE_SESSION(sFaultContext)
PORT_STATE_SESSION(freq)
PORT_STATE_SESSION(timer)
