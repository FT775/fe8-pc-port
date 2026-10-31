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
void PortAudioOut_Report(void);

#endif /* GUARD_PORT_AUDIO_AUDIO_PC_H */
