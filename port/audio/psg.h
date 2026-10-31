/*
 * port/audio/psg.h: the GBA's four Game Boy sound channels (the PSG) and the
 * sound mixer's DAC stage (SOUNDCNT_L/H/X, SOUNDBIAS), for the FE8 PC port.
 *
 * Derived from platform/psg.h and platform/psg*.c of pokeemerald-expansion-native,
 * https://github.com/uehlbran/pokeemerald-expansion-native, commit
 * bb3fd057061cdd7e6a9e79b76abb41924ee44ecc (2026-10-15). Changes for FE8 are
 * listed in psg.c. That project's licence follows, unchanged.
 *
 * ---------------------------------------------------------------------------
 * MIT License (scoped)
 *
 * Copyright (c) 2026 pokeemerald-expansion-native contributors
 *
 * Scope. This license covers only the files this project adds on top of
 * pokeemerald-expansion (the "Platform Layer"):
 *
 * - platform/ and tests/ (tests/smoke/ holds the input-replay scripts and the
 *   gate scripts, platform/build_glue/ the build checks)
 * - Makefile.native and tools/build-native.sh, and the files they include
 *   that are not part of upstream pokeemerald-expansion
 * - .github/workflows/native.yml, windows.yml and release.yml
 * - .github/actions/
 * - .github/ISSUE_TEMPLATE/
 * - .github/pull_request_template.md
 * - .github/release/
 * - .clang-format and .clang-tidy
 * - test/crit_odds.c, test/gba_table_reads.c, test/malloc.c, test/siirtc.c
 *   and test/trainer_util.c
 * - README.md, CONTRIBUTING.md and SECURITY.md
 * - docs/ARCHITECTURE.md, docs/BUILDING.md, docs/C-RULES.md,
 *   docs/DECISIONS.md, docs/RESEARCH.md, docs/RUNNING.md, docs/UPGRADING.md
 *   and docs/UPSTREAM_MODIFICATIONS.md
 * - docs/research/
 * - the first section of CREDITS.md
 *
 * It also covers the modifications this project made to upstream files, which
 * are listed in docs/UPSTREAM_MODIFICATIONS.md, to the extent those
 * modifications are separable from the upstream code.
 *
 * Files of the platform layer that translate or closely follow upstream's
 * code are covered only to the extent they are separable from the code they
 * follow. Those that translate decompiled code are platform/m4a/ (the C
 * translation of src/m4a_1.s, and the transcription of src/m4a.c in its tests)
 * and platform/decompress_asm.c (the routines of src/decompress_asm.s). Those
 * that follow upstream sources closely are platform/crt0.c (src/crt0.s),
 * platform/interrupt.c (IntrMain in src/crt0.s), platform/rtc_siirtc.c
 * (src/siirtc.c), platform/gcn_multiboot.c (src/libgcnmultiboot.s) and
 * platform/game_hooks.c (test/test_runner.c, test/test_runner_battle.c,
 * src/main.c and src/multiboot.c).
 *
 * It does not cover, and this project claims no rights over: the
 * pret/pokeemerald decompilation and the pokeemerald-expansion project and
 * their contributors' work; any code, graphics, audio, text or other
 * material owned by Nintendo, Creatures Inc. or GAME FREAK inc.; or any
 * third-party software such as SDL2. Those remain under whatever terms
 * apply to them.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of the Platform Layer, to deal in the Platform Layer without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies of the
 * Platform Layer, and to permit persons to whom the Platform Layer is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice, this scope statement and this permission
 * notice shall be included in all copies or substantial portions of the
 * Platform Layer.
 *
 * THE PLATFORM LAYER IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * PLATFORM LAYER OR THE USE OR OTHER DEALINGS IN THE PLATFORM LAYER.
 * ---------------------------------------------------------------------------
 *
 * WHAT THIS IS (port-plan/audio-design.md section 4, checkpoint P3.4).
 * On the GBA the PSG runs by itself once the game has written its registers.
 * On the PC the I/O region is plain memory, and a store does nothing by
 * itself. So once per VBlank, right after OnVBlank has run (CgbSound writes
 * the registers there), the frame step calls:
 *
 *     PortPsg_Latch();                              reads the register image
 *     PortPsg_Render(laneA, laneB, lr, 224);        runs one frame, mixes it
 *
 * PortPsg_Render also does the mixer's DAC stage for the two DirectSound
 * FIFOs, because on hardware the FIFOs and the PSG are added before the
 * bias and the clip (GBATEK "GBA Sound Control Registers", Max Output
 * Levels). The FIFO model passes the 224 bytes that each FIFO plays this
 * frame, or NULL for a FIFO whose DMA is off.
 *
 * REGISTER IMAGE RULES (sound.md section 5; GBATEK).
 * - Read back unchanged, because CgbSound reads them (m4a.c:911, 1119, 1136,
 *   1144, 1160): NR10, NR12, NR22, NR42, NR43, NR51 and SOUNDBIAS. This
 *   module never writes them, except that clearing the master enable resets
 *   4000060h-4000081h to zero, as the hardware does.
 * - NRx4 bit 7 (Initial) is write-only and reads 0 on hardware. A set bit
 *   counts as one trigger and is cleared in the image (DIVERGENCES H-7,
 *   audio-design's H-5).
 * - SOUNDCNT_X bits 0-3 are read-only channel on flags. They are rewritten
 *   after every Latch and Render; bits 4-6 read 0; bit 7 is kept.
 * - NR13 and NR14 bits 0-2 (write-only) receive channel 1's swept frequency,
 *   so the next Latch does not undo the sweep.
 * - Every other bit is left as the game wrote it.
 *
 * Integer only. Single-threaded: call from the thread that runs the game.
 */
#ifndef GUARD_PORT_AUDIO_PSG_H
#define GUARD_PORT_AUDIO_PSG_H

#include <stdint.h>

/* GBATEK "LCD Dimensions and Timings": 16.78 MHz = 2^24 Hz system clock,
 * 228 lines of 1232 cycles per frame. */
#define PORT_PSG_CLOCK_HZ 16777216u
#define PORT_PSG_FRAME_CYCLES 280896u
/* FE8's DirectSound rate: Timer 0 reload -1254 (m4a.c:378), 224 samples per
 * frame (m4a.c:365); 224 * 1254 = 280896, one frame exactly. */
#define PORT_PSG_SAMPLE_CYCLES 1254u
#define PORT_PSG_FRAME_SAMPLES 224u

/* Resets the model to its power-on state: every channel off, both wave RAM
 * banks zero, the frame sequencer stopped until SOUNDCNT_X bit 7 is seen
 * set. `io` is the base of the I/O register image (0x04000000 in the port;
 * any 0x400-byte buffer in the tests). Does not write the image. */
void PortPsg_Init(volatile uint8_t *io);

/* Writes the sound registers' boot values into the image, as the BIOS
 * leaves them (GBATEK "SOUNDBIAS ... Default=200h"; BIOS RegisterRamReset
 * with the sound flag): 4000060h-40000A7h zero, SOUNDBIAS = 0x0200.
 * audio-design 4.3: the port must start SOUNDBIAS at 0x0200, or the bias is
 * 0 and every negative DirectSound sample clips. Not called by this module. */
void PortPsg_ResetIo(volatile uint8_t *io);

/* Reads the register image once: SOUNDCNT_X, SOUNDCNT_L, SOUNDCNT_H,
 * SOUNDBIAS, NR10-NR44 and wave RAM. Acts on the trigger bits and clears
 * them. Call once per VBlank, after OnVBlank. */
void PortPsg_Latch(void);

/* Runs the hardware for `samples` * 1254 cycles and writes `samples`
 * interleaved stereo frames (left, right) to `lr`. laneA[i] and laneB[i] are
 * the FIFO samples played during frame i (NULL: silence). Each output frame
 * is the average of the DAC's output at the PWM ticks in its 1254 cycles
 * (65536 Hz with FE8's SOUNDBIAS), less the bias, at 64 per 10-bit unit:
 * a lone FIFO sample s at 100% gives exactly s << 8. Then republishes the
 * image bits listed above. */
void PortPsg_Render(const int8_t *laneA, const int8_t *laneB, int16_t *lr, uint32_t samples);

/* For tests and tools. */
void PortPsg_Run(uint32_t cycles);   /* advance the hardware, no output */
int PortPsg_ChannelOn(int ch);       /* ch 1-4: the SOUNDCNT_X on flag */
uint8_t PortPsg_ChannelLevel(int ch); /* ch 1-4: digital output now, 0-15 */

#endif /* GUARD_PORT_AUDIO_PSG_H */
