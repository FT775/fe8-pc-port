/*
 * FE8 PC port: the sound hardware after the driver (port-plan/audio-design.md
 * sections 1 and 3). The game's own VBlank handler runs the m4a driver
 * (m4aSoundVSync, m4aSoundMain), which fills the PCM ring in IWRAM and writes
 * the PSG registers. These calls stand in for what the GBA hardware does next:
 * Timer 0, DMA1/DMA2 and FIFO A/B (the FIFO model), the PSG and the DAC
 * (port/audio/psg.c), and the speaker (the platform's audio stream).
 *
 * Frame step order (port/hw/frame.c, line 160):
 *   PortAudio_PreVBlank();   tests only: the per-VBlank sound record
 *   Irq(0)                   OnVBlank: m4aSoundVSync ... m4aSoundMain
 *   PortAudio_OnVBlank();    this frame's 224 samples, every frame step
 *
 * 224 stereo frames per VBlank at 13,379 Hz, s16, interleaved L,R. The output
 * is the DAC's own (PortPsg_Render): no filter, no resampling here.
 */
#ifndef GUARD_PORT_AUDIO_AUDIO_PC_H
#define GUARD_PORT_AUDIO_AUDIO_PC_H

#include <stdint.h>

#define PORT_AUDIO_RATE_HZ          13379u /* pcmFreq, m4a.c:369 */
#define PORT_AUDIO_FRAMES_PER_VBLANK 224u  /* m4a.c:365; 224 * 1254 cycles = one frame */
/* v1.32.0, "Enhanced sound" (hq_pc.c): 4 frames per GBA sample */
#define PORT_AUDIO_HQ_RATE_HZ        (PORT_AUDIO_RATE_HZ * 4u)           /* 53,516 Hz */
#define PORT_AUDIO_HQ_FRAMES         (PORT_AUDIO_FRAMES_PER_VBLANK * 4u) /* 896 per VBlank */

/* fifo_pc.c */
void PortAudio_Init(void);       /* after Platform_Init */
void PortAudio_PreVBlank(void);  /* line 160, before the VBlank IRQ */
void PortAudio_OnVBlank(void);   /* line 160, after the VBlank IRQ (dispatched or not) */
void PortAudio_Report(void);     /* at exit: counters, closes the dump files */
/* Tests only: the index the next sound record gets (PortAudio_PreVBlank calls
   so far; port/test/pc/gate0_hook.c GATE0_SND_INDEX). */
uint32_t PortAudio_NextRecord(void);

/* audio_out.c: the queue to the platform's audio device (D4, audio-design 3.2).
   Never blocks the frame step; without a device it does nothing. */
void PortAudioOut_Init(void);
void PortAudioOut_Push(const int16_t *lr, uint32_t frames);
/* v1.32.0: frames at rateHz (13,379 or 53,516); the stream follows a change of rate. */
void PortAudioOut_PushRate(const int16_t *lr, uint32_t frames, uint32_t rateHz);

/* hq_pc.c (v1.32.0, played builds): "Enhanced sound", the shadow mixer. PortHq_SetRow: the Display screen's
   "Sound" row (Enhanced: 1); PortHq_SetDump: FE8PC_AUDIO_DUMP_HQ. PortHq_Active: the shadow is mixed (either);
   PortHq_Playing: the speaker plays it (the row). m4a_1_pc.c: PortHq_Frame when SoundMainRAM starts a slot,
   PortHq_Channel for each channel it mixes, before it does. fifo_pc.c: PortHq_LaneAt, the shadow lane played
   where the GBA plays the PCM ring's bytes at off. */
void PortHq_SetRow(int on);
void PortHq_SetDump(int on);
int PortHq_Active(void);
int PortHq_Playing(void);
void PortHq_Frame(int slotIndex, uint32_t samples, uint32_t reverb, int next);
void PortHq_Channel(int chanIndex, uint32_t wav, uint32_t cp, uint32_t fw, uint32_t step, int fixed, int loop,
                    uint32_t er, uint32_t el, int started);
const float *PortHq_LaneAt(uint32_t off, uint32_t bufSize);
/* fifo_pc.c: PortHq_PsgBegin before PortPsg_Render (the PSG's levels for the shadow), PortHq_Render after it (the
   shadow's DAC: 896 s16 L,R frames). reforged_savestate.c: PortHq_Reset after a save state's load. */
void PortHq_PsgBegin(void);
void PortHq_Render(const float *laneA, const float *laneB, int16_t *lr, uint32_t frames);
void PortHq_Reset(void);
void PortAudioOut_Report(void);

#endif /* GUARD_PORT_AUDIO_AUDIO_PC_H */
