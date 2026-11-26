#include "global.h"
#include "gba/m4a_internal.h"
#include "port_state.h" /* PORT_STATE_ON: a played build */

/*
 * "Enhanced sound" (RFO_AUDIO_HQ, the Display screen's "Sound" row: Original / Enhanced; v1.32.0): the music and sound effects
 * mixed again on the PC at 4 times the GBA's rate, for the speaker only. Off by default; display-only (it
 * never marks a save). Played builds only (PORT_STATE_ON); the test builds have none of it.
 *
 * The GBA's own pipeline is untouched and always runs: m4a_1_pc.c's SoundMainRAM mixes 224 bytes per lane
 * and VBlank into the PCM ring in IWRAM, fifo_pc.c plays the slot the DMA plays, psg.c runs the Game Boy
 * channels and the DAC. Every byte of GBA memory, the frames, the saves and the "Original" sound are the
 * same with the row on or off. With it on, three host-only additions:
 *
 *   1. PCM. m4a_1_pc.c calls PortHq_Frame when SoundMainRAM starts a slot (after the reverb or clear) and
 *      PortHq_Channel for each channel it mixes, just before it mixes it, with the channel as the GBA
 *      mixer is about to read it: the wave, the sample position (cp and the 9.23 fraction fw), the samples
 *      left, the step (divFreq * freq, 9.23 per GBA sample; one sample for a fixed-rate wave), the loop, and
 *      the volumes er / el it just took from the envelope. The same stretch of sound is mixed here into a
 *      shadow of that slot, 896 frames (4 x 224) of floats per lane: the step a quarter, the samples taken
 *      with 4-point cubic (Catmull-Rom) interpolation instead of the GBA's linear one, the volumes moved
 *      smoothly from the previous slot's to this slot's across the 896 frames instead of in one step, the
 *      sum kept whole instead of wrapping in 8 bits. The position is the GBA's at the start of every slot,
 *      so the two never drift apart. The reverb is the GBA's formula on the shadow slots (the same two
 *      slots, mono, reverb / 512).
 *   2. PSG. psg.c's PortPsg_Render, run exactly as for the original sound, hands each PWM tick's Game Boy
 *      channel level before the DAC (no bias, no 10-bit cut) to PsgTick here (psg.h PortPsg_SetTickHook;
 *      psg.c stays integer-only), which spreads it over 896 bins by the cycles each tick covers (a box filter
 *      at 53,516 Hz instead of 13,379 Hz).
 *   3. The DAC. fifo_pc.c tells which shadow slot each FIFO plays this VBlank (the one whose bytes its DMA
 *      plays, or none); PortHq_Render mixes those lanes and the PSG as SOUNDCNT_H routes them, in the DAC's
 *      units, with a soft knee instead of the hard clip, to 896 s16 frames at 53,516 Hz for the audio device
 *      (SDL resamples them to the device's rate).
 *   A save state's load clears the shadow (PortHq_Reset): it does not know the ring the load put back.
 *
 * FE8PC_AUDIO_DUMP_HQ=prefix (a test switch) writes prefix.lr16 (the 53,516 Hz s16 L,R frames, 896 per
 * VBlank) whether or not the row is on.
 */

#if PORT_STATE_ON

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port_hw.h"
#include "port_state.h"
#include "audio_pc.h"
#include "psg.h"

#define HQ_X     4u                                   /* frames per GBA sample */
#define HQ_N     (PORT_AUDIO_FRAMES_PER_VBLANK * HQ_X) /* 896 */
#define HQ_SLOTS 8                                    /* pcmDmaPeriod is 7 in FE8 */
#define HQ_CHANS 16

/* the shadow of the PCM ring: [slot][lane][frame], in the GBA's byte units (a channel adds vol * s / 256) */
static float sSlot[HQ_SLOTS][2][HQ_N];
static int sSlotNow = -1;      /* the slot SoundMainRAM is mixing now (PortHq_Frame) */
static uint32_t sSlotSamples;  /* its samples per slot (224) */
static struct
{
    uint32_t wav;  /* the wave the channel played last slot (0: none) */
    float er, el;  /* its volumes last slot */
    int wrapped;   /* it has gone round its loop: the sample before loopStart is the wave's last */
} sCh[HQ_CHANS];
/* the PSG's level before the DAC (psg.c's tick hook), in HQ_N bins of 1254 / 4 cycles (half cycles: 627 a bin):
   level x half cycles, and the half cycles each bin got */
#define HQ_BIN2 (PORT_PSG_SAMPLE_CYCLES * 2u / HQ_X)
static float sPsg[HQ_N][2], sPsgW[HQ_N];
static int sOn;   /* the shadow is mixed: the row on, or FE8PC_AUDIO_DUMP_HQ */
static int sRow;  /* the row: the speaker plays the shadow */
static int sDump; /* FE8PC_AUDIO_DUMP_HQ */

int PortHq_Active(void)
{
    return sOn;
}

int PortHq_Playing(void)
{
    return sRow;
}

static void Update(void)
{
    int on = sRow || sDump;

    if (on && !sOn)
    {
        memset(sSlot, 0, sizeof(sSlot));
        memset(sCh, 0, sizeof(sCh));
    }
    sOn = on;
}

/* The Display screen's "Sound" row (reforged_display_menu.c Reforged_ApplyDisplay). */
void PortHq_SetRow(int on)
{
    sRow = on != 0;
    Update();
}

/* fifo_pc.c: FE8PC_AUDIO_DUMP_HQ is set (the shadow is mixed and written, the row on or off). */
void PortHq_SetDump(int on)
{
    sDump = on != 0;
    Update();
}

/* SoundMainRAM, after its reverb or clear of the slot at slotIndex (samples per slot), reverb its strength
   (0: the slot was cleared), next the slot the reverb read as the older one. */
void PortHq_Frame(int slotIndex, uint32_t samples, uint32_t reverb, int next)
{
    float (*s)[HQ_N], (*o)[HQ_N];
    uint32_t i;

    sSlotNow = -1;
    if (!sOn || slotIndex < 0 || slotIndex >= HQ_SLOTS || next < 0 || next >= HQ_SLOTS ||
        samples != PORT_AUDIO_FRAMES_PER_VBLANK)
        return;
    sSlotNow = slotIndex;
    sSlotSamples = samples;
    s = sSlot[slotIndex];
    o = sSlot[next];
    if (reverb == 0)
    {
        memset(s, 0, sizeof(sSlot[0]));
        return;
    }
    /* the GBA's: v = (this L + this R + older L + older R) * reverb >> 9, on both lanes */
    for (i = 0; i < HQ_N; i++)
    {
        float v = (s[0][i] + s[1][i] + o[0][i] + o[1][i]) * (float)reverb / 512.0f;

        if (fabsf(v) < 1e-6f)
            v = 0.0f; /* the GBA's integer echo dies at 0; floats would linger in denormals */
        s[0][i] = v;
        s[1][i] = v;
    }
}

static inline float Sample(const int8_t *data, int32_t k, int32_t size, int32_t loopStart, int loop, int wrapped)
{
    if (k < loopStart && wrapped && loop && size > loopStart)
        k += size - loopStart; /* just after going round: the sample before loopStart is the wave's end */
    if (k >= size)
    {
        if (!loop || size <= loopStart)
            return 0.0f;
        k = loopStart + (k - loopStart) % (size - loopStart);
    }
    if (k < 0)
        k = 0;
    return (float)data[k];
}

/* SoundMainRAM, each channel it mixes, just before: chanIndex, the wave header (wav: type, ..., loopStart
   at +8, size at +0xC, data at +0x10), the GBA's position (cp: the address of the current sample; fw: the
   9.23 fraction past it; 0 for a fixed-rate wave), step (9.23 per GBA sample; ignored for a fixed-rate
   wave), loop, the volumes er (lane 0) and el (lane 1), started: the channel began this slot. */
void PortHq_Channel(int chanIndex, uint32_t wav, uint32_t cp, uint32_t fw, uint32_t step, int fixed, int loop,
                    uint32_t er, uint32_t el, int started)
{
    const int8_t *data;
    int32_t size, loopStart, k;
    double pos, inc;
    float er0, el0, der, del;
    float *l0, *l1;
    uint32_t i;

    if (!sOn || sSlotNow < 0 || chanIndex < 0 || chanIndex >= HQ_CHANS || wav == 0)
        return;
    data = (const int8_t *)(uintptr_t)(wav + 0x10);
    memcpy(&size, (const void *)(uintptr_t)(wav + 0xC), 4);
    memcpy(&loopStart, (const void *)(uintptr_t)(wav + 0x8), 4);
    if (size <= 0 || loopStart < 0 || loopStart > size)
        return;
    pos = (double)(int32_t)(cp - (wav + 0x10)) + (fixed ? 0.0 : (double)fw / 8388608.0);
    inc = fixed ? 1.0 / HQ_X : (double)step / 8388608.0 / HQ_X;
    if (started || sCh[chanIndex].wav != wav)
    {
        sCh[chanIndex].er = (float)er;
        sCh[chanIndex].el = (float)el;
        sCh[chanIndex].wrapped = 0;
    }
    er0 = sCh[chanIndex].er, el0 = sCh[chanIndex].el;
    der = ((float)er - er0) / HQ_N, del = ((float)el - el0) / HQ_N;
    sCh[chanIndex].wav = wav;
    sCh[chanIndex].er = (float)er;
    sCh[chanIndex].el = (float)el;
    l0 = sSlot[sSlotNow][0];
    l1 = sSlot[sSlotNow][1];
    for (i = 0; i < HQ_N; i++)
    {
        float t, p0, p1, p2, p3, v;

        if (pos >= (double)size)
        {
            if (!loop || size <= loopStart)
                break;
            pos = loopStart + fmod(pos - loopStart, (double)(size - loopStart));
            sCh[chanIndex].wrapped = 1;
        }
        k = (int32_t)pos;
        t = (float)(pos - k);
        p0 = Sample(data, k - 1, size, loopStart, loop, sCh[chanIndex].wrapped);
        p1 = Sample(data, k, size, loopStart, loop, sCh[chanIndex].wrapped);
        p2 = Sample(data, k + 1, size, loopStart, loop, sCh[chanIndex].wrapped);
        p3 = Sample(data, k + 2, size, loopStart, loop, sCh[chanIndex].wrapped);
        /* Catmull-Rom */
        v = p1 + 0.5f * t * (p2 - p0 + t * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3 + t * (3.0f * (p1 - p2) + p3 - p0)));
        l0[i] += v * (er0 + der * (float)(i + 1)) / 256.0f;
        l1[i] += v * (el0 + del * (float)(i + 1)) / 256.0f;
        pos += inc;
    }
}

/* fifo_pc.c: the shadow lane that plays where the GBA plays the PCM ring's bytes at off (from pcmBuffer),
   bufSize bytes a lane; NULL for none. */
const float *PortHq_LaneAt(uint32_t off, uint32_t bufSize)
{
    uint32_t lane, slot;

    if (!sOn)
        return NULL;
    lane = off >= bufSize ? 1u : 0u;
    off -= lane * bufSize;
    if (off % PORT_AUDIO_FRAMES_PER_VBLANK != 0)
        return NULL;
    slot = off / PORT_AUDIO_FRAMES_PER_VBLANK;
    return slot < HQ_SLOTS ? sSlot[slot][lane] : NULL;
}

/* psg.c's tick hook: a tick's level over the half cycles it stands for, into the bins it covers */
static void PsgTick(uint32_t cycleEnd, uint32_t period, int32_t l, int32_t r)
{
    int32_t a = (int32_t)(2u * cycleEnd) - (int32_t)(2u * period), b = (int32_t)(2u * cycleEnd);

    if (a < 0)
        a = 0; /* the part before this render: its bins were last VBlank's */
    while (a < b)
    {
        int32_t bin = a / (int32_t)HQ_BIN2, end = (bin + 1) * (int32_t)HQ_BIN2;

        if (bin >= (int32_t)HQ_N)
            break;
        if (end > b)
            end = b;
        sPsg[bin][0] += (float)l * (float)(end - a);
        sPsg[bin][1] += (float)r * (float)(end - a);
        sPsgW[bin] += (float)(end - a);
        a = end;
    }
}

/* fifo_pc.c, before PortPsg_Render: the PSG's bins for this VBlank (psg.c calls PsgTick while the shadow is
   mixed). */
void PortHq_PsgBegin(void)
{
    if (sOn)
    {
        memset(sPsg, 0, sizeof(sPsg));
        memset(sPsgW, 0, sizeof(sPsgW));
    }
    PortPsg_SetTickHook(sOn ? PsgTick : NULL);
}

/* fifo_pc.c, after PortPsg_Render: the DAC for the shadow. The two shadow lanes the FIFOs play (NULL: that FIFO
   silent) and the PSG's bins, routed by SOUNDCNT_H as psg.c's DAC routes them (FIFO x2, or x4 at 100%), in the
   DAC's units around the bias, without the DAC's cut to 9 or 10 bits, a soft knee for its clip; s16 L,R. */
void PortHq_Render(const float *laneA, const float *laneB, int16_t *lr, uint32_t frames)
{
    uint16_t cnth, bias16;
    int powered;
    float bias, hi, lo, ka, kb, pl = 0.0f, pr = 0.0f;
    uint32_t i;

    PortPsg_DacRegs(&cnth, &bias16, &powered);
    bias = (float)(bias16 & 0x03FEu);
    hi = 1023.0f - bias;
    lo = bias;            /* the room below the bias */
    ka = (cnth & 0x0004u) ? 4.0f : 2.0f;
    kb = (cnth & 0x0008u) ? 4.0f : 2.0f;
    for (i = 0; i < frames && i < HQ_N; i++)
    {
        float a = laneA != NULL && powered ? laneA[i] * ka : 0.0f, b = laneB != NULL && powered ? laneB[i] * kb : 0.0f;
        float v[2];
        int c;

        if (sPsgW[i] > 0.0f) /* a bin the ticks covered only in part (the first): its own average */
            pl = sPsg[i][0] / sPsgW[i], pr = sPsg[i][1] / sPsgW[i];
        v[0] = ((cnth & 0x0200u) ? a : 0.0f) + ((cnth & 0x2000u) ? b : 0.0f) + pl;
        v[1] = ((cnth & 0x0100u) ? a : 0.0f) + ((cnth & 0x1000u) ? b : 0.0f) + pr;
        for (c = 0; c < 2; c++)
        {
            float x = v[c], lim = x >= 0.0f ? hi : lo, knee = 0.75f * lim, y = x, s;
            long n;

            if (lim - knee <= 0.0f)
                y = 0.0f; /* no room on that side (a bias at an end): silence rather than a division by 0 */
            else if (x > knee || x < -knee)
            {
                float over;

                s = x >= 0.0f ? 1.0f : -1.0f;
                over = (s * x - knee) / (lim - knee);
                y = s * (knee + (lim - knee) * over / (1.0f + over));
            }
            n = lrintf(y * 64.0f); /* psg.c S16_PER_UNIT: 0x200 units = 32768 */
            lr[2 * i + c] = (int16_t)(n > 32767 ? 32767 : n < -32768 ? -32768 : n);
        }
    }
}

/* A save state's load (reforged_savestate.c, with Platform_AudioJoin): the shadow of the ring the load put back
   is not known; it starts silent rather than playing the slots of before the load (and their echo). */
void PortHq_Reset(void)
{
    memset(sSlot, 0, sizeof(sSlot));
    memset(sCh, 0, sizeof(sCh));
}

PORT_STATE_SESSION(sPsg)
PORT_STATE_SESSION(sPsgW)
PORT_STATE_SESSION(sSlot)
PORT_STATE_SESSION(sSlotNow)
PORT_STATE_SESSION(sSlotSamples)
PORT_STATE_SESSION(sCh)
PORT_STATE_SESSION(sOn)
PORT_STATE_SESSION(sRow)
PORT_STATE_SESSION(sDump)

#endif /* PORT_STATE_ON */
