/*
 * Test-build check of PROC_CALL_ARG targets (DIVERGENCES.md D-29 and B-17,
 * host-ABI values). Linked only into the Gate 0 test build (port/test/Makefile
 * `pc`), whose game code is compiled with -DFE8PC_DEBUG=1 (port/test/pc/gate0.mk).
 * The shipped builds have neither, so their behaviour does not change.
 *
 * The value PROC_CALL_ARG's target returns decides whether the proc script
 * goes on (RunProcessScript, src/proc.c). For a void target that value is a
 * register left over from the call: r0 on the GBA, eax on the host.
 * ProcCmd_CALL_ROUTINE_ARG (src/proc.c) returns the GBA's r0 for the six void
 * targets of the retail ROM, and calls PortDebug_CheckProcCallArg for any other
 * target. A target that really returns its s8 is listed in VETTED_TARGETS
 * below; any other target is reported, once, on a "[gate0] PROC_CALL_ARG:"
 * line, which fails check-all's Gate 0 rows. So a new void target (from a
 * source change or a mod) is not silently given host eax.
 *
 * To clear a report: name the target (addr2line -f -e build/pc-gate0/fe8pc
 * ADDRESS). If it is void, work out the GBA's r0 at its return from its code
 * and add it to ProcCmd_CALL_ROUTINE_ARG; if it returns s8, add it to
 * VETTED_TARGETS. `python3 port/tools/proc_call_arg_scan.py --check` checks
 * both lists against the PROC_CALL_ARG commands in the built ROM.
 */
#include <stddef.h>
#include <stdint.h>

#include "port_hw.h"

void PortDebug_CheckProcCallArg(const void *func, const void *cmd);

/* PROC_CALL_ARG targets that return their s8, one VETTED(Name) per line. The
   retail ROM has none: all 102 of its commands call one of the six void
   functions handled in src/proc.c. Declared with a dummy type, as only the
   addresses are compared. */
#define VETTED_TARGETS \
    /* none */

#define VETTED(name) extern void name(void);
VETTED_TARGETS
#undef VETTED
#define VETTED(name) (const void *)name,
static const void *const sVetted[] = { VETTED_TARGETS NULL };
#undef VETTED

#define MAX_REPORTED 64
static const void *sReported[MAX_REPORTED];
static unsigned sReportedCount;

void PortDebug_CheckProcCallArg(const void *func, const void *cmd)
{
    unsigned i;

    for (i = 0; sVetted[i] != NULL; i++)
        if (sVetted[i] == func)
            return;

    for (i = 0; i < sReportedCount; i++)
        if (sReported[i] == func)
            return;

    if (sReportedCount < MAX_REPORTED)
        sReported[sReportedCount++] = func;

    PortLog("[gate0] PROC_CALL_ARG: target 0x%08X (command at 0x%08X) is not a known target; if it is "
            "void, the host returns eax where the GBA returns r0 (port/test/pc/proc_call_arg_check.c)\n",
            (unsigned)(uintptr_t)func, (unsigned)(uintptr_t)cmd);
}
