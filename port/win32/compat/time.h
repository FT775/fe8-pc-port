#ifndef GUARD_PORT_WIN32_COMPAT_TIME_H
#define GUARD_PORT_WIN32_COMPAT_TIME_H

/*
 * Windows build only (port/Makefile.mingw puts port/win32/compat first on the
 * port code's include path; game code never sees it). MinGW declares
 * clock_gettime and clock_nanosleep as inline wrappers around
 * clock_gettime32 and clock_nanosleep32 in libwinpthread, which this static
 * build does not link, so gen_host_link.py would turn them into silent stubs
 * (port/hw/platform_headless.c: Platform_NowNs, Platform_SleepUntilNs).
 * posix_win32.c implements the CLOCK_MONOTONIC subset that file uses, on
 * QueryPerformanceCounter and a waitable timer.
 */

#include_next <time.h>

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif
#ifndef TIMER_ABSTIME
#define TIMER_ABSTIME 1
#endif

int PortWin32ClockGetTime(int clock, struct timespec *ts);
int PortWin32ClockNanosleep(int clock, int flags, const struct timespec *request, struct timespec *remain);

#define clock_gettime   PortWin32ClockGetTime
#define clock_nanosleep PortWin32ClockNanosleep

#endif /* GUARD_PORT_WIN32_COMPAT_TIME_H */
