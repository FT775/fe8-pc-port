#include <stdint.h>
#include <stdlib.h>

#include "port_hw.h"
#include "../platform.h"

/*
 * Frame pacing (port-plan/PLAN.md phase 4, checkpoint P4.4).
 *
 * The GBA LCD runs one frame every 280,896 cycles of its 16,777,216 Hz clock:
 * 280896 / 16777216 s = 16,742,706.298828125 ns, or 59.7275 Hz. The pacer
 * keeps an accumulator of frame deadlines on Platform_NowNs. Each frame adds
 * 16,742,706 ns and carries the remaining 153/512 ns, so the long-run rate is
 * exact (no drift). After each frame step it sleeps until the next deadline.
 * Presenting a frame never moves the schedule: the frame clock drives the
 * game logic, never the display (decision D4). On a 60 Hz display a frame
 * therefore repeats about every 3.7 s, which is expected (PLAN.md phase 4).
 *
 * Method adapted from pokeemerald-expansion-native, platform/frame_pacer.c
 * (MIT, scoped to its platform layer; https://github.com/uehlbran/pokeemerald-
 * expansion-native, read at bb3fd057; port-plan/community-reuse-render.md
 * section 2.4): exact deadlines with a carried fraction, and after a stall of
 * more than 250 ms the schedule restarts from now, instead of running the
 * missed frames back to back. No code was copied: here the fraction is kept
 * in 1/512 ns and the clock is the platform interface's.
 *
 * Environment:
 *   FE8PC_PACE=0|1   override the default: on for an interactive backend (a
 *                    window), off headless, where frames run back to back.
 *   FE8PC_SECONDS=s  stop after s seconds of wall-clock time, logging the
 *                    frame count. P4.4: 60 s gives 3,584 +- 1 frames.
 */

#define PACE_FRAME_NS       16742706u /* whole nanoseconds per GBA frame */
#define PACE_FRAC_PER_FRAME 153u      /* plus 153/512 ns */
#define PACE_FRAC_ONE       512u
#define PACE_MAX_STALL_NS   250000000u
#define NS_PER_SECOND       1000000000u

static int sPaceOn;
static uint64_t sStartNs, sDeadlineNs, sLimitNs;
static uint32_t sFrac, sFrames, sRestarts;

void PortPaceInit(int realtime)
{
    const char *s = getenv("FE8PC_PACE");

    sPaceOn = s != NULL ? atoi(s) != 0 : realtime;
    s = getenv("FE8PC_SECONDS");
    sLimitNs = s != NULL ? (uint64_t)(strtod(s, NULL) * NS_PER_SECOND) : 0;
    sStartNs = Platform_NowNs();
    sDeadlineNs = sStartNs;
    sFrac = 0;
    sFrames = 0;
    sRestarts = 0;
    if (sPaceOn || sLimitNs != 0)
        PortLog("[pace] %s, %u + %u/%u ns per frame (59.7275 Hz)%s\n",
            sPaceOn ? "real time" : "unthrottled", PACE_FRAME_NS, PACE_FRAC_PER_FRAME,
            PACE_FRAC_ONE, sLimitNs != 0 ? ", wall-clock limit set" : "");
}

/* Frames since PortPaceInit, the wall time they took, and the rate. */
void PortPaceReport(void)
{
    uint64_t elapsed;

    if (!sPaceOn && sLimitNs == 0)
        return;
    elapsed = Platform_NowNs() - sStartNs;
    PortLog("[pace] %u frames in %.3f s wall clock (%.4f Hz; GBA 59.7275 Hz), "
            "%u schedule restarts\n", sFrames, (double)elapsed / NS_PER_SECOND,
        elapsed != 0 ? (double)sFrames * NS_PER_SECOND / (double)elapsed : 0.0, sRestarts);
}

/* Called once at the end of every frame step. */
void PortPaceFrame(void)
{
    uint64_t now;

    sFrames++;
    if (sPaceOn)
    {
        sDeadlineNs += PACE_FRAME_NS;
        sFrac += PACE_FRAC_PER_FRAME;
        if (sFrac >= PACE_FRAC_ONE)
        {
            sFrac -= PACE_FRAC_ONE;
            sDeadlineNs++;
        }
        now = Platform_NowNs();
        if (now > sDeadlineNs + PACE_MAX_STALL_NS)
        {
            /* A long stall (the host was busy, or the process was stopped):
               start a new schedule here rather than catch up at full speed. */
            if (++sRestarts <= 8)
                PortLog("[pace] frame %u: %.1f ms behind, schedule restarted\n", gPortFrameCount,
                    (double)(now - sDeadlineNs) / 1e6);
            sDeadlineNs = now;
            sFrac = 0;
        }
        else
            Platform_SleepUntilNs(sDeadlineNs);
    }
    if (sLimitNs != 0 && Platform_NowNs() - sStartNs >= sLimitNs)
    {
        PortLog("[pace] wall-clock limit of %.3f s reached\n", (double)sLimitNs / NS_PER_SECOND);
        PortExit(0);
    }
}
