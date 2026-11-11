/*
 * Reforged Options: fast-forward (port-plan/DECISIONS.md 2026-10-28
 * "Fast-forward"; port-plan/reforged-next-briefs.md item 2). Played window
 * builds only: the SDL platform layer (port/sdl3/platform_sdl3.c, outside
 * FE8PC_TESTBUILD) is the only caller, and this file is empty in every test
 * build (PORT_PLAYED_BUILD, compiled with the game's flags like the rest of
 * port/reforged). Nothing here changes the game: the same frame steps run in
 * the same order with the same keys; only how fast they follow each other in
 * real time, which of them are shown, and how their sound is played change.
 *
 * ON by default (the user's exception to "every option off": it only acts
 * while the key is held). Hold Space, or press in the right stick of a
 * gamepad. While held:
 *   - Time. The pacer (port/hw/pacer.c, unchanged) keeps its GBA-rate
 *     schedule on Platform_NowNs. The platform's clock runs N times as fast
 *     as real time while the key is held (Reforged_FastForwardClock), and
 *     Platform_SleepUntilNs turns the pacer's deadline back into real time
 *     (Reforged_FastForwardRealTime), so the pacer runs N frame steps per GBA
 *     frame time with no change of its own. On a press or a release the clock
 *     is rebased at that instant, so it never jumps: no burst of catch-up
 *     frames after a release and no wait. (The pacer's "[pace]" report and
 *     FE8PC_SECONDS therefore count game time, not wall time, after a
 *     fast-forward; the "[ff]" lines here give the real times.)
 *   - Picture. Only every N-th frame is presented (Reforged_FastForwardPresent),
 *     the first one at once; every frame is still drawn by the renderer.
 *   - Sound. The game makes N times as many samples per real second; the SDL
 *     stream plays them N times as fast (its frequency ratio times N,
 *     Reforged_FastForwardAudioRatio). audio_out.c (unchanged) holds
 *     Platform_AudioQueued near 672 frames (50 ms); the platform reports the
 *     stream's count scaled (Reforged_FastForwardQueued, QueueTarget), so
 *     672 x N^1.5 input frames are held while fast-forwarding (100 ms of real
 *     time at 4x; one device buffer can take more than 672 frames then). On a
 *     press or a release production changes at once, and the multiplier
 *     glides to the new speed (exponentially, a time constant worked out
 *     from the queue: about 50-120 ms), so that the queue ends at the new
 *     speed's target: it fills on a press and drains on a
 *     release, never running dry or over (a queue already past that point:
 *     no glide).
 *     "Fast-forward sound":
 *       Mute (default): the same, at gain 0. The gain ramps down and back up
 *       over about 12 ms of real time (Reforged_FastForwardGain), so neither
 *       the press nor the release clicks. A release after a press of 250 ms or
 *       more does not glide: the stream (muted samples only by then) is emptied
 *       and refilled with 50 ms of silence, and the multiplier is 1 at once, so
 *       sound comes back at normal pitch (v1.1.1; the glide played the first
 *       normal samples sped up and sliding down, a whine). A press does not
 *       glide either: the multiplier stays 1 while the normal samples already
 *       queued play out and fade, then goes to N at once over silence (v1.1.2;
 *       the glide played them sped up and rising, a squeak).
 *       Sped up: the sound plays N times as fast (and higher), resampled by
 *       SDL's own resampler (the same one the 0.5% drift control uses), at
 *       2x, 3x and 4x. At 8x it is muted as with Mute (decided for
 *       rfo-int4, port-plan/pending/rfo-int4.md): SDL 3.4.2's resampler
 *       does not low-pass, so at 8x everything the GBA makes above about
 *       2.8 kHz folds back into the audible band as false tones (rfo-ff
 *       refute bug 1; 4x folds only 5.5-6.7 kHz to about 17-22 kHz, 2x and
 *       3x are clean).
 *
 * Settings (port/reforged/reforged_settings.c): fast_forward (on/off),
 * fast_forward_speed (2, 3, 4 or 8; default 4), fast_forward_sound (mute or
 * sped_up; default mute).
 *
 * Test hooks (environment; unset = nothing changes):
 *   FE8PC_FF_HOLD="a-b,c-d,..." holds the fast-forward key from frame a to
 *     frame b - 1 (gPortFrameCount at the end of each frame step), as if
 *     Space were held; up to 16 ranges. For scripted runs of the window build.
 *   FE8PC_FF_CHECK=PATH writes one line per frame step to PATH: the frame,
 *     the speed it ran at, FNV-1a hashes of the GBA's EWRAM+IWRAM, of
 *     I/O+palette+VRAM+OAM and of the samples the frame made (before the
 *     mute gain), then (not game state) the times the SDL stream was short
 *     when the device asked for sound (an underrun: it then plays silence),
 *     the real time in ms and the stream's multiplier. A run with
 *     fast-forward held must give the same first five columns as one without,
 *     but for the speed column (port-plan/pending/rfo-ff.md).
 */

#if PORT_PLAYED_BUILD

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port_hw.h"
#include "port_reforged.h"
#include "../platform.h"

#define FF_RAMP_FRAMES 160u   /* gain ramp: 160 GBA sound frames (12 ms) of real time */
#define FF_GAIN_ONE    32768
#define FF_HOLD_MAX    16
#define FF_LOG_MAX     32     /* "[ff]" press/release lines logged */
/* port/audio: the sound rate, and audio_out.c's queue target (3 VBlanks) */
#define FF_RATE_HZ     13379.0
#define FF_Q_TARGET    672.0
#define FF_GLIDE_MAX_NS 200000000.0 /* the longest glide time constant */
#define FF_GLIDE_SNAP  0.002        /* the glide ends this close to the speed */
/* A release's glide aims 224 frames (one VBlank) above the target: the last
   part of a long release (8x) found the stream 95 frames short of a 1024-frame
   device buffer without it; audio_out's own 0.5% drift ratio takes the rest. */
#define FF_RELEASE_MARGIN 224.0
/* A muted release flushes the stream only after a press this long: by then the audible
   samples queued before the press have played (50 ms at a rising multiplier), so all it
   holds is muted; a shorter tap glides as before. */
#define FF_FLUSH_MIN_NS 250000000u
static int sFlush;                      /* a flush for the platform to take */
/* A muted press does not glide up either: gliding, the normal samples still queued (about
   50 ms) played sped up and rising (a squeak). The multiplier stays 1 until they and the
   gain's ramp down have played (sPressHoldUntil), then goes to N at once, inaudibly (the
   stream holds silence from there). The queue grows meanwhile (N - 1 times the rate), well
   below N's target. */
static uint64_t sPressHoldUntil;

static int sSpeed = 1;                  /* N while fast-forwarding, else 1 */
static double sMul = 1.0;               /* the stream's multiplier now (glides to sSpeed) */
static double sGlideNs;                 /* the glide's time constant now (for the log) */
static uint64_t sGlideAt;               /* the time sMul stands for */
static unsigned sShown;                 /* frames since the press, for the present */
static uint64_t sBaseReal, sBaseVirt;   /* the clock: virt = base + (real - base) * sSpeed */
static double sRatioBase = 1.0;         /* audio_out's own ratio (drift control) */
static int sGain = FF_GAIN_ONE;         /* Q15 gain on the pushed samples */
static uint32_t sPressFrame, sLogged;
static uint64_t sPressReal;
static int sHoldCount = -1;
static uint32_t sHoldFrom[FF_HOLD_MAX], sHoldTo[FF_HOLD_MAX];
static FILE *sCheck;
static int sCheckOn = -1;
static uint32_t sSndHash = 2166136261u;
static volatile uint32_t sShortCount, sShortBytes, sShortBig, sShortMax; /* FE8PC_FF_CHECK: audio thread's */
static uint32_t sShortLogged;
static uint64_t sCheckReal;
static unsigned sCheckQueued;

static uint32_t Fnv(uint32_t h, const void *p, uint32_t n)
{
    const unsigned char *b = p;

    while (n--)
        h = (h ^ *b++) * 16777619u;
    return h;
}

static int ScriptHeld(void)
{
    int i;

    if (sHoldCount < 0)
    {
        const char *s = getenv("FE8PC_FF_HOLD");
        int n;

        sHoldCount = 0;
        while (s != NULL && *s != '\0' && sHoldCount < FF_HOLD_MAX &&
               sscanf(s, "%u-%u%n", &sHoldFrom[sHoldCount], &sHoldTo[sHoldCount], &n) >= 2)
        {
            sHoldCount++;
            s += n;
            if (*s == ',')
                s++;
        }
        if (sHoldCount > 0)
            PortLog("[ff] test hook FE8PC_FF_HOLD: %d ranges, the first %u-%u\n", sHoldCount, sHoldFrom[0],
                sHoldTo[0]);
    }
    for (i = 0; i < sHoldCount; i++)
        if (gPortFrameCount >= sHoldFrom[i] && gPortFrameCount < sHoldTo[i])
            return 1;
    return 0;
}

/* The speed the settings give while the key is held: 2, 3, 4 or 8. */
static int HeldSpeed(void)
{
    int n = Reforged_GetOption(RFO_FF_SPEED);

    return n == 2 || n == 3 || n == 4 || n == 8 ? n : 4;
}

/* The highest speed "Sped up" sound plays at; above it the sound is muted
   (see the top of this file). */
#define FF_SOUND_MAX_SPEED 4

/* 1 while fast-forwarding with the sound heard (Sped up at 2x-4x), 0 muted. */
static int SoundHeard(int speed)
{
    return Reforged_GetOption(RFO_FF_SOUND) && speed <= FF_SOUND_MAX_SPEED;
}

static void CheckLine(void)
{
    uint32_t ram, vid, io;

    if (sCheckOn < 0)
    {
        const char *path = getenv("FE8PC_FF_CHECK");

        sCheckOn = 0;
        if (path != NULL && *path != '\0')
        {
            sCheck = fopen(path, "w");
            if (sCheck != NULL)
                sCheckOn = 1;
            else
                PortLog("[ff] FE8PC_FF_CHECK: cannot write %s\n", path);
        }
    }
    if (!sCheckOn)
        return;
    ram = Fnv(2166136261u, (const void *)(uintptr_t)0x02000000u, 0x40000);  /* EWRAM */
    ram = Fnv(ram, (const void *)(uintptr_t)0x03000000u, 0x8000);           /* IWRAM */
    io = Fnv(2166136261u, (const void *)(uintptr_t)0x04000000u, 0x400);     /* I/O */
    vid = Fnv(2166136261u, (const void *)(uintptr_t)0x05000000u, 0x400);    /* palette */
    vid = Fnv(vid, (const void *)(uintptr_t)0x06000000u, 0x18000);          /* VRAM */
    vid = Fnv(vid, (const void *)(uintptr_t)0x07000000u, 0x400);            /* OAM */
    fprintf(sCheck, "%u %d %08X %08X %08X %u %.3f %.4f %u %u %08X %u\n", gPortFrameCount, sSpeed, ram, vid,
        sSndHash, sShortCount, sCheckReal / 1e6, sMul, sShortBig, sShortMax, io, sCheckQueued);
    sSndHash = 2166136261u;
    {
        /* FE8PC_FF_DUMP="f1,f2,...": the GBA's RAM, I/O, palette, VRAM and OAM at those frames
           (CHECKPATH.<frame>.mem), to find what a differing hash is */
        const char *s = getenv("FE8PC_FF_DUMP");
        char want[16], path[4200];
        FILE *f;

        snprintf(want, sizeof(want), "%u", gPortFrameCount);
        while (s != NULL && *s != '\0')
        {
            size_t n = strcspn(s, ",");

            if (n == strlen(want) && strncmp(s, want, n) == 0)
            {
                snprintf(path, sizeof(path), "%s.%u.mem", getenv("FE8PC_FF_CHECK"), gPortFrameCount);
                f = fopen(path, "wb");
                if (f != NULL)
                {
                    fwrite((const void *)(uintptr_t)0x02000000u, 1, 0x40000, f);
                    fwrite((const void *)(uintptr_t)0x03000000u, 1, 0x8000, f);
                    fwrite((const void *)(uintptr_t)0x04000000u, 1, 0x400, f);
                    fwrite((const void *)(uintptr_t)0x05000000u, 1, 0x400, f);
                    fwrite((const void *)(uintptr_t)0x06000000u, 1, 0x18000, f);
                    fwrite((const void *)(uintptr_t)0x07000000u, 1, 0x400, f);
                    fclose(f);
                }
            }
            s += n;
            if (*s == ',')
                s++;
        }
    }
    if (sShortBig != sShortLogged && sShortLogged < 20)
    {
        PortLog("[ff] check: the stream was short by more than 64 frames %u times (largest %u bytes), "
                "frame %u\n", sShortBig, sShortMax, gPortFrameCount);
        sShortLogged = sShortBig;
    }
    if ((gPortFrameCount & 255) == 0)
        fflush(sCheck);
}

uint64_t Reforged_FastForwardClock(uint64_t realNs)
{
    return sBaseVirt + (realNs - sBaseReal) * (uint64_t)sSpeed;
}

uint64_t Reforged_FastForwardRealTime(uint64_t clockNs)
{
    if (clockNs <= sBaseVirt)
        return sBaseReal;
    return sBaseReal + (clockNs - sBaseVirt + (uint64_t)sSpeed - 1) / (uint64_t)sSpeed;
}

/* The stream's queued input frames audio_out's target (672) stands for at
   multiplier m: 672 x m^1.5, i.e. 50 ms of real time x sqrt(m) (71 ms at
   2x, 100 ms at 4x, 141 ms at 8x). More than 50 ms while fast-forwarding, so
   a release, whose glide drains the queue back to 672 while the device
   still takes large buffers, has room (a 1024-frame device buffer at 44.1
   kHz takes 310 x m input frames). */
static double QueueTarget(double m)
{
    return FF_Q_TARGET * m * sqrt(m);
}

/*
 * The multiplier's glide toward sSpeed, once per frame step; 1 when it moved.
 * Production is sSpeed at once; consumption follows m(t) = speed + (m - speed)
 * e^(-t/tau), which changes the queue by RATE x tau x (speed - m) in all. tau
 * is worked out again at every step from the queue now, so the queue ends at
 * the new speed's target, QueueTarget(speed) (plus a VBlank on a release),
 * whatever the device actually took meanwhile (a device that plays slower
 * than its rate, as SDL's disk driver on Windows, makes tau longer). From a
 * steady queue tau is ~120 ms on a press and ~100-150 ms on a release. A
 * queue already at or past the target (fuller on a press, emptier on a
 * release): the multiplier goes to the speed at once, so a release never
 * drains a short queue.
 * The multiplier set now holds until the next frame step (one GBA frame time
 * / speed from now): the glide is taken half way there, so the stream's
 * consumption over the step is the glide's own (the value at the start took
 * ~(from - speed) / 2 x 224 frames too many over a release).
 */
static int Glide(unsigned queued, uint64_t realNs)
{
    double m, fill, tau, dt;
    uint64_t at;

    if (sMul == (double)sSpeed)
        return 0;
    if (sPressHoldUntil != 0)
    {
        /* a muted press: 1 until what was queued has played, then N at once */
        if (sSpeed == 1 || realNs >= sPressHoldUntil)
        {
            sPressHoldUntil = 0;
            sMul = (double)sSpeed;
            return 1;
        }
        return 0;
    }
    at = realNs + 16742706u / (2u * (unsigned)sSpeed);
    dt = at > sGlideAt ? (double)(at - sGlideAt) : 0.0;
    sGlideAt = at;
    fill = QueueTarget(sSpeed) - (double)queued;
    if (sSpeed < sMul)
        fill += FF_RELEASE_MARGIN;
    tau = 1e9 * fill / (FF_RATE_HZ * (sSpeed - sMul));
    if (tau > FF_GLIDE_MAX_NS)
        tau = FF_GLIDE_MAX_NS;
    m = tau <= 0 ? (double)sSpeed : sSpeed + (sMul - sSpeed) * exp(-dt / tau);
    if (fabs(m - sSpeed) < FF_GLIDE_SNAP)
        m = (double)sSpeed;
    sGlideNs = tau > 0 ? tau : 0;
    if (m == sMul)
        return 0;
    sMul = m;
    return 1;
}

int Reforged_FastForwardFrame(int keyHeld, unsigned queued, uint64_t realNs)
{
    int want = 1;

    sCheckReal = realNs;
    sCheckQueued = queued;
    CheckLine(); /* the frame step that just ended, at the speed it ran at */

    if (Reforged_GetOption(RFO_FAST_FORWARD) && (keyHeld || ScriptHeld()))
        want = HeldSpeed();
    {
        /* "Enemy phase" (reforged_enemyphase.c): its speed, the faster of the two */
        int enemy = Reforged_EnemyPhaseSpeed();

        if (enemy > want)
            want = enemy;
    }
    if (want == sSpeed)
        return Glide(queued, realNs);

    /* Rebase the clock at this instant: it goes on from the same value. */
    sBaseVirt = Reforged_FastForwardClock(realNs);
    sBaseReal = realNs;
    /* A muted fast-forward released (held long enough that what the stream holds is muted
       samples only): no glide down. Gliding, the first normal samples played while the
       multiplier was still above 1, so the sound came back high and slid down (a whine).
       The platform empties the stream and refills it with silence at the normal target
       (Reforged_FastForwardTakeFlush); the gain ramps up from 0 as after any release. */
    if (want == 1 && sSpeed > 1 && !SoundHeard(sSpeed) && realNs - sPressReal >= FF_FLUSH_MIN_NS)
    {
        sMul = 1.0;
        sFlush = 1;
    }
    sPressHoldUntil = 0;
    if (want > 1 && sSpeed == 1 && sMul == 1.0 && !SoundHeard(want))
        /* the queue and the gain's ramp down (FF_RAMP_FRAMES x N input frames), both played
           at 1x, and a frame step's margin */
        sPressHoldUntil = realNs + (uint64_t)((queued + FF_RAMP_FRAMES * (double)want) * 1e9 / FF_RATE_HZ) +
                          16742706u;
    sSpeed = want;
    sGlideAt = realNs; /* the glide toward `want` starts now */
    Glide(queued, realNs);
    if (want > 1)
    {
        sShown = 0;
        sPressFrame = gPortFrameCount;
        sPressReal = realNs;
        if (sLogged++ < FF_LOG_MAX)
            PortLog("[ff] fast-forward %dx from frame %u (sound %s; queue %u, glide %.1f ms)\n", want,
                gPortFrameCount, SoundHeard(want) ? "sped up" : Reforged_GetOption(RFO_FF_SOUND) ? "muted at 8x" : "muted",
                queued, sGlideNs / 1e6);
    }
    else if (sLogged <= FF_LOG_MAX)
    {
        double ms = (double)(realNs - sPressReal) / 1e6;
        uint32_t frames = gPortFrameCount - sPressFrame;

        PortLog("[ff] normal speed from frame %u: %u frames in %.1f ms (%.2fx; queue %u, glide %.1f ms)\n",
            gPortFrameCount, frames, ms, ms > 0 ? frames * 16.742706 / ms : 0.0, queued, sGlideNs / 1e6);
    }
    return 1;
}

int Reforged_FastForwardTakeFlush(unsigned *silence)
{
    if (!sFlush)
        return 0;
    sFlush = 0;
    *silence = (unsigned)FF_Q_TARGET;
    return 1;
}

int Reforged_FastForwardPresent(void)
{
    if (sSpeed == 1)
        return 1;
    return sShown++ % (unsigned)sSpeed == 0;
}

double Reforged_FastForwardAudioRatio(double base)
{
    if (base > 0)
        sRatioBase = base;
    return sRatioBase * sMul;
}

void Reforged_FastForwardStreamShort(int additional, int total)
{
    (void)total;
    if (additional > 0)
    {
        sShortCount++;
        sShortBytes += (uint32_t)additional;
        if (additional > 256) /* more than 64 frames: beyond SDL's request estimate (see rfo-ff.md) */
            sShortBig++;
        if ((uint32_t)additional > sShortMax)
            sShortMax = (uint32_t)additional;
    }
}

unsigned Reforged_FastForwardQueued(unsigned frames)
{
    /* (a muted press's hold: the queue is filling toward N's target, so it is counted as at N,
       or audio_out would take it for an overfull 1x queue and drop VBlanks) */
    double m = sPressHoldUntil != 0 ? (double)sSpeed : sMul;

    return m == 1.0 ? frames : (unsigned)(frames * FF_Q_TARGET / QueueTarget(m));
}

int Reforged_FastForwardGain(const short *in, short *out, unsigned frames)
{
    int target = sSpeed > 1 && !SoundHeard(sSpeed) ? 0 : FF_GAIN_ONE;
    int step = (int)((FF_GAIN_ONE + FF_RAMP_FRAMES * sSpeed - 1) / (FF_RAMP_FRAMES * sSpeed));
    unsigned i;

    if (sCheckOn > 0)
        sSndHash = Fnv(sSndHash, in, frames * 2 * sizeof(short));
    if (sGain == FF_GAIN_ONE && target == FF_GAIN_ONE)
        return 0; /* as made: the caller queues `in` */
    for (i = 0; i < frames; i++)
    {
        if (sGain < target)
            sGain = sGain + step < target ? sGain + step : target;
        else if (sGain > target)
            sGain = sGain - step > target ? sGain - step : target;
        out[2 * i] = (short)((in[2 * i] * sGain) >> 15);
        out[2 * i + 1] = (short)((in[2 * i + 1] * sGain) >> 15);
    }
    return 1;
}

#endif /* PORT_PLAYED_BUILD */
