#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port_hw.h"
#include "audio_pc.h"
#include "../platform.h"

/*
 * Audio output queue (port-plan/audio-design.md section 3; decision D4: the
 * frame clock rules). Library-neutral: it only uses the platform calls.
 *
 * The frame step makes exactly 224 frames per VBlank; that never changes.
 * The device consumes at its own clock, which drifts against the pacer. The
 * queue is held near 3 VBlanks (672 frames) by changing only how fast the
 * device stream plays, by at most 0.5% (Platform_SetAudioRatio):
 *   qs += (q - qs) / 32 once per VBlank;
 *   r = 1 + 0.005 * clamp((qs - 672) / 672, -1, 1), sent when it moves by
 *   more than 0.0002.
 * Safety nets: above 12 VBlanks queued, whole VBlanks are dropped until it is
 * below 8; when the queue runs dry the device is paused and refilled to 3
 * VBlanks before it plays again. Neither ever waits: the frame step never
 * blocks on audio, and no extra frame is ever run to refill the queue.
 *
 * FE8PC_AUDIO=0 opens no device (the game then runs silent, as headless).
 */

#define VB PORT_AUDIO_FRAMES_PER_VBLANK
#define Q_TARGET (3u * VB)  /* 672 frames, 50 ms */
#define Q_HIGH   (12u * VB) /* drop VBlanks above this ... */
#define Q_LOW    (8u * VB)  /* ... until below this */
#define MAX_DEVIATION 0.005
#define RATIO_STEP 0.0002

static int sDevice;   /* Platform_OpenAudio gave a device */
static int sPlaying;  /* device resumed */
static int sDropping;
static double sQs, sRatio = 1.0;
static uint64_t sQueued, sDropped;
static uint32_t sUnderruns, sRatioSets, sQMin = UINT32_MAX, sQMax, sPushes;
static double sRatioMin = 1.0, sRatioMax = 1.0;

void PortAudioOut_Init(void)
{
    const char *s = getenv("FE8PC_AUDIO");

    if (s != NULL && strcmp(s, "0") == 0)
    {
        PortLog("[audio] FE8PC_AUDIO=0: no audio device\n");
        return;
    }
    sDevice = Platform_OpenAudio(PORT_AUDIO_RATE_HZ, 2) != 0;
    if (sDevice)
        PortLog("[audio] device open: %u Hz stereo s16, queue target %u frames\n", PORT_AUDIO_RATE_HZ,
            Q_TARGET);
}

void PortAudioOut_Push(const int16_t *lr, uint32_t frames)
{
    uint32_t q;
    double r, dev;

    if (!sDevice)
        return;
    q = Platform_AudioQueued();
    if (sPlaying && q == 0)
    {
        /* Ran dry (a stall): the device has been playing silence. Refill first. */
        if (sUnderruns++ < 8)
            PortLog("[audio] queue ran dry at frame %u; refilling to %u frames\n", gPortFrameCount, Q_TARGET);
        Platform_PauseAudio(1);
        sPlaying = 0;
    }
    if (sDropping && q < Q_LOW)
        sDropping = 0;
    else if (!sDropping && q > Q_HIGH)
    {
        sDropping = 1;
        PortLog("[audio] queue at %u frames: dropping VBlanks until it is below %u\n", q, Q_LOW);
    }
    if (sDropping)
    {
        sDropped += frames;
        return;
    }

    Platform_QueueAudio(lr, frames);
    sQueued += frames;
    q = Platform_AudioQueued();
    if (!sPlaying)
    {
        if (q < Q_TARGET)
            return; /* prefill */
        Platform_PauseAudio(0);
        sPlaying = 1;
        sQs = (double)q;
        return;
    }

    sPushes++;
    if (q < sQMin)
        sQMin = q;
    if (q > sQMax)
        sQMax = q;
    sQs += ((double)q - sQs) / 32.0;
    dev = (sQs - (double)Q_TARGET) / (double)Q_TARGET;
    if (dev > 1.0)
        dev = 1.0;
    if (dev < -1.0)
        dev = -1.0;
    r = 1.0 + MAX_DEVIATION * dev;
    if (r - sRatio > RATIO_STEP || sRatio - r > RATIO_STEP)
    {
        Platform_SetAudioRatio(r);
        sRatio = r;
        sRatioSets++;
        if (r < sRatioMin)
            sRatioMin = r;
        if (r > sRatioMax)
            sRatioMax = r;
    }
}

void PortAudioOut_Report(void)
{
    if (!sDevice)
    {
        PortLog("[audio] no audio device: nothing queued\n");
        return;
    }
    PortLog("[audio] queued to the device: %llu frames (%.3f s at %u Hz), dropped %llu, ran dry %u times\n",
        (unsigned long long)sQueued, (double)sQueued / PORT_AUDIO_RATE_HZ, PORT_AUDIO_RATE_HZ,
        (unsigned long long)sDropped, sUnderruns);
    if (sPushes != 0)
        PortLog("[audio] queue after each push: %u to %u frames (target %u); ratio %.4f now, %.4f to %.4f, "
                "set %u times\n", sQMin, sQMax, Q_TARGET, sRatio, sRatioMin, sRatioMax, sRatioSets);
}
