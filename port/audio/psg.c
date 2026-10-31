/*
 * port/audio/psg.c: the GBA's four Game Boy sound channels (the PSG) and the
 * sound mixer's DAC stage, for the FE8 PC port. Interface and register rules:
 * psg.h. Design: port-plan/audio-design.md section 4 (checkpoint P3.4).
 *
 * Derived from platform/psg.c, psg_channel.c, psg_square.c, psg_wave.c,
 * psg_noise.c and psg_internal.h of pokeemerald-expansion-native,
 * https://github.com/uehlbran/pokeemerald-expansion-native, commit
 * bb3fd057061cdd7e6a9e79b76abb41924ee44ecc (2026-10-15). That project's
 * licence follows, unchanged.
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
 * CHANGES FROM UPSTREAM (FE8 port, 2026-10-19)
 * - One file, port names (PortPsg_*). The register image is reached through
 *   a base pointer, so the tests can pass a plain buffer.
 * - Reading and running are split: PortPsg_Latch once per VBlank, then
 *   PortPsg_Render for 224 samples of 1254 cycles (FE8's Timer 0 rate)
 *   instead of point sampling at a host rate.
 * - Adds the mixer's DAC stage that upstream left out: FIFO A/B volume and
 *   routing (SOUNDCNT_H), the SOUNDBIAS bias, the clip to 10 bits, the
 *   amplitude resolution and the PWM sampling grid (SOUNDBIAS bits 14-15).
 * - Length counters follow the Game Boy rule (an NRx1 write loads the
 *   counter, a trigger reloads it only when it has run out) instead of
 *   loading at every trigger. m4a retriggers on every volume change.
 * - PortPsg_ResetIo writes the BIOS boot values (SOUNDBIAS = 0x0200).
 * Kept from upstream: the trigger-bit consumption, the wave RAM bank rule,
 * the sweep write-back to NR13/NR14, the status bits, the master-disable
 * reset, the frame sequencer, GBATEK's noise generator and the PSG scale.
 *
 * SOURCES. Behaviour from GBATEK "GBA Sound Controller" (channels 1-4,
 * "GBA Sound Control Registers", Max Output Levels, SOUNDBIAS) and, where
 * GBATEK is silent, Pan Docs "Audio Details" (sweep internals, length and
 * envelope units, the 512 Hz frame sequencer). Facts only from mGBA; no
 * MPL or GPL code.
 *
 * TIME. Everything counts system clock cycles (2^24 Hz). The frame
 * sequencer steps every 32768 cycles (512 Hz): length on steps 0, 2, 4 and
 * 6 (256 Hz), sweep on 2 and 6 (128 Hz), envelope on 7 (64 Hz). It starts
 * at step 0 when the master enable is first seen set; its first step comes
 * 32768 cycles later. On the GBA its phase against VBlank depends on boot
 * timing, which the port does not copy, so PSG audio cannot be bit-exact
 * (audio-design 4.3). The register stream can be.
 *
 * OUTPUT. GBATEK: each FIFO spans +/-200h of the 10-bit output (sample * 4
 * at 100%, * 2 at 50%); each PSG channel spans a quarter (level * 10h at
 * full volume). The PSG side is sum(levels) * 8 * (NR50 volume + 1) >>
 * (4 - SOUNDCNT_H bits 0-1), so 100% gives level * 10h at NR50 = 7. The sum
 * of PSG and FIFOs plus the bias (SOUNDBIAS bits 1-9) is clipped to 0..3FFh
 * and cut to the resolution (bits 14-15: 9, 8, 7 or 6 bits) at the sampling
 * rate (32768 << n Hz, one tick every 512 >> n cycles). FE8 sets 0x4200:
 * bias 200h, 8 bits, 65536 Hz (m4a.c:332). The ticks sit on multiples of
 * the tick period counted from PortPsg_Init. Render averages the ticks in
 * each 1254-cycle sample (4 or 5 at 65536 Hz), removes the bias and scales
 * by 64 per 10-bit unit, rounding toward zero. The PSG's own DC offset
 * stays in (the GBA's output capacitor removes it).
 *
 * AMBIGUITIES (choices, all unused by FE8 unless noted).
 * - SOUNDCNT_H PSG volume 3 ("Prohibited") plays as 100%.
 * - Wave RAM banks: hardware sends each window write to the bank not
 *   selected at that moment. The latch sees the window once per VBlank, so a
 *   window that changed since the last latch goes to the bank selected at
 *   the latch. That is right for m4a's "NR30 = 40h, write, NR30 = 0"
 *   (m4a.c:929-938, used by FE8) and for "write the idle bank, then select
 *   it". Rewriting identical bytes is invisible.
 * - Length: an NRx1 write is seen only as a change of the byte, so a
 *   rewrite of the same value does not reload the counter. FE8's voices all
 *   have length 0, so its length counters are never enabled.
 * - 75% wave volume is (3 * sample) >> 2.
 * Not modelled: the Game Boy's power-on and trigger-timing quirks, the NR10
 * negate quirk, the wave channel's first-sample delay, wave RAM reads while
 * playing, and noise shifts 14-15 (GBATEK gives no exception).
 */
#include "psg.h"

#include <stddef.h>
#include <string.h>

/* I/O offsets, GBATEK "GBA Sound Channel 1" .. "GBA Sound Control
 * Registers" (include/gba/io_reg.h has the same). */
#define REG_NR10 0x060u
#define REG_NR11 0x062u
#define REG_NR12 0x063u
#define REG_NR13 0x064u
#define REG_NR14 0x065u
#define REG_NR21 0x068u
#define REG_NR22 0x069u
#define REG_NR23 0x06Cu
#define REG_NR24 0x06Du
#define REG_NR30 0x070u
#define REG_NR31 0x072u
#define REG_NR32 0x073u
#define REG_NR33 0x074u
#define REG_NR34 0x075u
#define REG_NR41 0x078u
#define REG_NR42 0x079u
#define REG_NR43 0x07Cu
#define REG_NR44 0x07Du
#define REG_NR50 0x080u /* SOUNDCNT_L low byte */
#define REG_NR51 0x081u /* SOUNDCNT_L high byte */
#define REG_SOUNDCNT_H 0x082u
#define REG_NR52 0x084u /* SOUNDCNT_X */
#define REG_SOUNDBIAS 0x088u
#define REG_WAVE_RAM 0x090u
#define REG_FIFO_END 0x0A8u /* FIFO_B ends at 40000A7h */

/* SOUNDCNT_X bit 7, "PSG/FIFO Master Enable": clearing it resets
 * 4000060h-4000081h to zero. */
#define NR52_MASTER 0x80u
#define REG_PSG_FIRST REG_NR10
#define REG_PSG_END (REG_NR51 + 1u)

/* NRx4: bit 7 Initial (write-only), bit 6 Length Flag, bits 0-2 the
 * frequency's top bits. */
#define NRX4_INITIAL 0x80u
#define NRX4_LENGTH 0x40u
#define NRX4_FREQ_HIGH 0x07u

/* NRx2 of channels 1, 2, 4: bits 0-2 step time (n/64 s, 0 = none), bit 3
 * direction (1 = increase), bits 4-7 initial volume. Bits 3-7 all zero
 * switch the DAC off (Pan Docs), which stops the channel. */
#define NRX2_DAC 0xF8u

/* SOUNDCNT_H: bits 0-1 PSG volume (25/50/100%), bit 2/3 FIFO A/B 100%,
 * bits 8/9 A right/left, bits 12/13 B right/left. */
#define CNTH_PSG_VOLUME 0x0003u
#define CNTH_A_FULL 0x0004u
#define CNTH_B_FULL 0x0008u
#define CNTH_A_RIGHT 0x0100u
#define CNTH_A_LEFT 0x0200u
#define CNTH_B_RIGHT 0x1000u
#define CNTH_B_LEFT 0x2000u

/* SOUNDBIAS: bits 1-9 bias level, bits 14-15 resolution / sampling rate. */
#define BIAS_LEVEL 0x03FEu
#define BIAS_RESOLUTION_SHIFT 14u
#define BIAS_BOOT 0x0200u

#define SEQ_CYCLES 32768u   /* 2^24 / 512 */
#define LEVEL_MAX 15u
#define FREQ_MAX 2047u
#define NOT_SEEN 0x100u     /* no NRx1 byte seen since power-on */
#define S16_PER_UNIT 64     /* 0x200 units * 64 = 32768 */

struct Length
{
    uint16_t counter; /* 256 Hz ticks left; 0 = run out */
    uint8_t enabled;  /* NRx4 bit 6 */
};

struct Envelope
{
    uint8_t volume;   /* 0-15 */
    uint8_t period;   /* 64 Hz ticks per step; 0 = no envelope */
    uint8_t timer;
    uint8_t increase;
};

struct Square
{
    uint8_t on;
    struct Length length;
    struct Envelope env;
    uint8_t duty;     /* NRx1 bits 6-7 */
    uint8_t dutyStep; /* 0-7 */
    uint16_t freq;    /* 11 bits */
    uint32_t timer;   /* cycles to the next duty step */
    uint16_t nrx1Seen;
};

struct Sweep
{
    uint8_t nr10;
    uint16_t shadow;
    uint8_t timer;    /* 128 Hz ticks to the next sweep step */
    uint8_t enabled;
};

struct Wave
{
    uint8_t on;
    struct Length length;
    uint8_t nr30, nr32;
    uint16_t freq;
    uint32_t timer;   /* cycles to the next sample */
    uint8_t pos;      /* 0-31, or 0-63 with two banks */
    uint8_t bank[2][16];
    uint8_t windowSeen[16];
    uint16_t nr31Seen;
};

struct Noise
{
    uint8_t on;
    struct Length length;
    struct Envelope env;
    uint8_t nr43;
    uint16_t lfsr;
    uint8_t high;     /* the last bit shifted out */
    uint32_t timer;   /* cycles to the next LFSR clock */
    uint16_t nr41Seen;
};

struct SquareRegs
{
    uint32_t nrx1, nrx2, nrx3, nrx4;
};

static const struct SquareRegs sCh1Regs = { REG_NR11, REG_NR12, REG_NR13, REG_NR14 };
static const struct SquareRegs sCh2Regs = { REG_NR21, REG_NR22, REG_NR23, REG_NR24 };

static struct
{
    volatile uint8_t *io;
    uint8_t powered;      /* SOUNDCNT_X bit 7 at the last latch */
    uint32_t cycle;       /* cycles since PortPsg_Init, mod 2^32 */
    uint32_t seqTimer;    /* cycles to the next sequencer step */
    uint8_t seqStep;      /* the step that comes next, 0-7 */
    struct Square sq[2];
    struct Sweep sweep;
    uint16_t ch1FreqImage; /* NR13/NR14 frequency as last read or written */
    struct Wave wave;
    struct Noise noise;
    uint8_t nr50, nr51;
    uint16_t soundcntH, soundbias;
    uint32_t pwmTicks;    /* DAC ticks rendered, for tests */
} sPsg;

/* --- Register image ------------------------------------------------------- */

static uint8_t Rd(uint32_t off)
{
    return sPsg.io[off];
}

static void Wr(uint32_t off, uint8_t value)
{
    sPsg.io[off] = value;
}

static uint16_t Rd16(uint32_t off)
{
    return (uint16_t)(Rd(off) | (uint32_t)Rd(off + 1) << 8);
}

/* NRx4 bit 7 set since the last latch: one trigger. Clears it (psg.h). */
static int TakeTrigger(uint32_t nrx4)
{
    uint8_t value = Rd(nrx4);

    if (!(value & NRX4_INITIAL))
        return 0;
    Wr(nrx4, (uint8_t)(value & ~NRX4_INITIAL));
    return 1;
}

/* --- Shared units --------------------------------------------------------- */

/* GBATEK "Sound length; units of (64-n)/256s" ((256-n) for channel 3). */
static void LengthSeeWrite(struct Length *len, uint16_t *seen, uint8_t nrx1, uint16_t units, uint8_t mask)
{
    if (*seen == nrx1)
        return;
    *seen = nrx1;
    len->counter = (uint16_t)(units - (nrx1 & mask));
}

static void LengthTrigger(struct Length *len, uint16_t units)
{
    if (len->counter == 0)
        len->counter = units;
}

static void LengthClock(struct Length *len, uint8_t *on)
{
    if (!len->enabled || len->counter == 0)
        return;
    if (--len->counter == 0)
        *on = 0;
}

/* GBATEK "Envelope Step-Time; units of n/64s (1-7, 0=No Envelope)". */
static void EnvelopeTrigger(struct Envelope *env, uint8_t nrx2)
{
    env->volume = (uint8_t)(nrx2 >> 4);
    env->period = (uint8_t)(nrx2 & 7u);
    env->timer = env->period;
    env->increase = (nrx2 & 8u) != 0;
}

static void EnvelopeClock(struct Envelope *env)
{
    if (env->period == 0)
        return;
    if (--env->timer != 0)
        return;
    env->timer = env->period;
    if (env->increase && env->volume < LEVEL_MAX)
        env->volume++;
    else if (!env->increase && env->volume > 0)
        env->volume--;
}

static int DacOn(uint8_t nrx2)
{
    return (nrx2 & NRX2_DAC) != 0;
}

/* A frequency timer: runs `cycles` cycles, returns how many times it
 * expired. Each expiry reloads from `period`, so a new frequency takes
 * effect from the next step. *timer and period are at least 1. */
static uint32_t TimerRun(uint32_t *timer, uint32_t period, uint32_t cycles)
{
    uint32_t after;

    if (cycles < *timer)
    {
        *timer -= cycles;
        return 0;
    }
    after = cycles - *timer;
    *timer = period - after % period;
    return 1 + after / period;
}

/* --- Channels 1 and 2 ---------------------------------------------------- */

/* GBATEK "Wave Duty": 12.5% -_______, 25% --______, 50% ----____,
 * 75% ------__. Step 0 in bit 0. */
static const uint8_t sDutyPatterns[4] = { 0x01, 0x03, 0x0F, 0x3F };

/* GBATEK "Frequency = 131072/(2048-n)Hz", 8 duty steps per period:
 * (2048-n) * 16 cycles a step. */
static uint32_t SquarePeriod(uint16_t freq)
{
    return (2048u - freq) * 16u;
}

/* Returns 1 if the channel was triggered and started. */
static int ApplySquare(struct Square *sq, const struct SquareRegs *regs)
{
    int trigger = TakeTrigger(regs->nrx4);
    uint8_t nrx1 = Rd(regs->nrx1);
    uint8_t nrx2 = Rd(regs->nrx2);
    uint8_t nrx3 = Rd(regs->nrx3);
    uint8_t nrx4 = Rd(regs->nrx4);

    sq->duty = (uint8_t)(nrx1 >> 6);
    LengthSeeWrite(&sq->length, &sq->nrx1Seen, nrx1, 64, 0x3F);
    sq->length.enabled = (nrx4 & NRX4_LENGTH) != 0;
    sq->freq = (uint16_t)(nrx3 | (uint32_t)(nrx4 & NRX4_FREQ_HIGH) << 8);
    if (!DacOn(nrx2))
    {
        sq->on = 0;
        return 0;
    }
    if (!trigger)
        return 0;
    sq->on = 1;
    LengthTrigger(&sq->length, 64);
    sq->timer = SquarePeriod(sq->freq); /* the duty step is kept (Pan Docs) */
    EnvelopeTrigger(&sq->env, nrx2);
    return 1;
}

static void SquareRun(struct Square *sq, uint32_t cycles)
{
    if (sq->on)
        sq->dutyStep = (uint8_t)((sq->dutyStep + TimerRun(&sq->timer, SquarePeriod(sq->freq), cycles)) & 7u);
}

static uint8_t SquareLevel(const struct Square *sq)
{
    if (!sq->on || !((sDutyPatterns[sq->duty] >> sq->dutyStep) & 1u))
        return 0;
    return sq->env.volume;
}

/* Channel 1's sweep, NR10: bits 0-2 shift n, bit 3 decrease, bits 4-6 time
 * (units of 1/128 s). GBATEK: X(t) = X(t-1) +/- X(t-1)/2^n. Internals from
 * Pan Docs: a shadow frequency taken at the trigger; an overflow check at
 * the trigger when n != 0; on each step a new frequency is written only if
 * n != 0 and it fits, then checked again; above 2047 stops the channel;
 * time 0 reloads the timer with 8 and does no steps. */
static uint32_t SweepNext(void)
{
    const struct Sweep *sw = &sPsg.sweep;
    uint32_t delta = (uint32_t)sw->shadow >> (sw->nr10 & 7u);

    return (sw->nr10 & 8u) ? sw->shadow - delta : sw->shadow + delta;
}

static uint8_t SweepTime(void)
{
    return (uint8_t)((sPsg.sweep.nr10 >> 4) & 7u);
}

static uint8_t SweepShift(void)
{
    return (uint8_t)(sPsg.sweep.nr10 & 7u);
}

static void SweepReload(void)
{
    sPsg.sweep.timer = SweepTime() != 0 ? SweepTime() : 8;
}

static void SweepTrigger(void)
{
    struct Sweep *sw = &sPsg.sweep;

    sw->shadow = sPsg.sq[0].freq;
    SweepReload();
    sw->enabled = SweepTime() != 0 || SweepShift() != 0;
    if (SweepShift() != 0 && SweepNext() > FREQ_MAX)
        sPsg.sq[0].on = 0;
}

static void SweepClock(void)
{
    struct Sweep *sw = &sPsg.sweep;
    uint32_t next;

    if (!sPsg.sq[0].on)
        return;
    if (sw->timer > 1)
    {
        sw->timer--;
        return;
    }
    SweepReload();
    if (!sw->enabled || SweepTime() == 0)
        return;
    next = SweepNext();
    if (next > FREQ_MAX)
    {
        sPsg.sq[0].on = 0;
        return;
    }
    if (SweepShift() == 0)
        return;
    sw->shadow = (uint16_t)next;
    sPsg.sq[0].freq = (uint16_t)next;
    if (SweepNext() > FREQ_MAX)
        sPsg.sq[0].on = 0;
}

/* --- Channel 3 ------------------------------------------------------------ */

/* NR30: bit 5 two banks, bit 6 bank number, bit 7 playback (the DAC).
 * NR32: bits 5-6 volume (0 mute, 1 100%, 2 50%, 3 25%), bit 7 force 75%.
 * NR33/NR34: "Sample Rate; 2097152/(2048-n) Hz", (2048-n) * 8 cycles. */
#define NR30_TWO_BANKS 0x20u
#define NR30_BANK 0x40u
#define NR30_PLAY 0x80u

static uint32_t WavePeriod(uint16_t freq)
{
    return (2048u - freq) * 8u;
}

static unsigned WaveSelected(void)
{
    return (sPsg.wave.nr30 & NR30_BANK) ? 1u : 0u;
}

static void ApplyWave(void)
{
    struct Wave *w = &sPsg.wave;
    int trigger = TakeTrigger(REG_NR34);
    uint8_t window[16];
    uint8_t nr34;
    unsigned i;

    w->nr30 = Rd(REG_NR30);
    w->nr32 = Rd(REG_NR32);
    nr34 = Rd(REG_NR34);
    for (i = 0; i < 16; i++)
        window[i] = Rd(REG_WAVE_RAM + i);
    if (memcmp(window, w->windowSeen, 16) != 0)
    {
        memcpy(w->bank[WaveSelected()], window, 16); /* see AMBIGUITIES */
        memcpy(w->windowSeen, window, 16);
    }
    LengthSeeWrite(&w->length, &w->nr31Seen, Rd(REG_NR31), 256, 0xFF);
    w->length.enabled = (nr34 & NRX4_LENGTH) != 0;
    w->freq = (uint16_t)(Rd(REG_NR33) | (uint32_t)(nr34 & NRX4_FREQ_HIGH) << 8);
    if (!(w->nr30 & NR30_PLAY))
    {
        w->on = 0;
        return;
    }
    if (!trigger)
        return;
    w->on = 1;
    LengthTrigger(&w->length, 256);
    w->pos = 0;
    w->timer = WavePeriod(w->freq);
}

static void WaveRun(uint32_t cycles)
{
    struct Wave *w = &sPsg.wave;
    uint32_t count = (w->nr30 & NR30_TWO_BANKS) ? 64u : 32u;

    if (w->on)
        w->pos = (uint8_t)((w->pos + TimerRun(&w->timer, WavePeriod(w->freq), cycles)) % count);
}

/* GBATEK: 32 4-bit samples a bank, "MSBs of 1st byte, followed by LSBs of
 * 1st byte"; with two banks the selected bank plays first. */
static uint8_t WaveSample(void)
{
    const struct Wave *w = &sPsg.wave;
    unsigned index = w->pos & 31u;
    unsigned bank = WaveSelected();
    uint8_t byte;

    if ((w->nr30 & NR30_TWO_BANKS) && w->pos >= 32)
        bank ^= 1u;
    byte = w->bank[bank][index >> 1];
    return (uint8_t)((index & 1u) ? byte & 0x0F : byte >> 4);
}

static uint8_t WaveLevel(void)
{
    const struct Wave *w = &sPsg.wave;
    uint8_t sample;
    unsigned volume;

    if (!w->on)
        return 0;
    sample = WaveSample();
    if (w->nr32 & 0x80u)
        return (uint8_t)((sample * 3u) >> 2);
    volume = (w->nr32 >> 5) & 3u;
    if (volume == 0)
        return 0;
    return (uint8_t)(sample >> (volume - 1u));
}

/* --- Channel 4 ------------------------------------------------------------ */

/* NR43: bits 0-2 r, bit 3 width (1 = 7 bits), bits 4-7 s. GBATEK:
 * "Frequency = 524288 Hz / r / 2^(s+1) ;For r=0 assume r=0.5 instead",
 * so r * 32 << (s+1) cycles a clock (16 << (s+1) for r = 0); at most
 * 7 * 32 << 16, inside 32 bits. */
static uint32_t NoisePeriod(uint8_t nr43)
{
    uint32_t r = nr43 & 7u;

    return (r != 0 ? r * 32u : 16u) << ((nr43 >> 4) + 1u);
}

/* GBATEK: "The initial value when (re-)starting the sound is X=40h (7bit)
 * or X=4000h (15bit)", then per clock "X=X SHR 1, IF carry THEN Out=HIGH,
 * X=X XOR 60h (7bit) / 6000h (15bit) ELSE Out=LOW". */
static uint16_t LfsrSeed(int sevenBit)
{
    return sevenBit ? 0x40u : 0x4000u;
}

static uint16_t LfsrClock(uint16_t x, int sevenBit)
{
    uint16_t shifted = (uint16_t)(x >> 1);

    if (!(x & 1u))
        return shifted;
    return (uint16_t)(shifted ^ (sevenBit ? 0x60u : 0x6000u));
}

static void ApplyNoise(void)
{
    struct Noise *n = &sPsg.noise;
    int trigger = TakeTrigger(REG_NR44);
    uint8_t nr42 = Rd(REG_NR42);
    uint8_t nr44 = Rd(REG_NR44);

    n->nr43 = Rd(REG_NR43);
    LengthSeeWrite(&n->length, &n->nr41Seen, Rd(REG_NR41), 64, 0x3F);
    n->length.enabled = (nr44 & NRX4_LENGTH) != 0;
    if (!DacOn(nr42))
    {
        n->on = 0;
        return;
    }
    if (!trigger)
        return;
    n->on = 1;
    LengthTrigger(&n->length, 64);
    n->lfsr = LfsrSeed(n->nr43 & 8u);
    n->high = 0;
    n->timer = NoisePeriod(n->nr43);
    EnvelopeTrigger(&n->env, nr42);
}

static void NoiseRun(uint32_t cycles)
{
    struct Noise *n = &sPsg.noise;
    uint32_t clocks;

    if (!n->on)
        return;
    clocks = TimerRun(&n->timer, NoisePeriod(n->nr43), cycles);
    while (clocks--)
    {
        n->high = (uint8_t)(n->lfsr & 1u);
        n->lfsr = LfsrClock(n->lfsr, n->nr43 & 8u);
    }
}

static uint8_t NoiseLevel(void)
{
    return (sPsg.noise.on && sPsg.noise.high) ? sPsg.noise.env.volume : 0;
}

/* --- Running -------------------------------------------------------------- */

static void SequencerStep(void)
{
    uint8_t step = sPsg.seqStep;

    if ((step & 1u) == 0)
    {
        LengthClock(&sPsg.sq[0].length, &sPsg.sq[0].on);
        LengthClock(&sPsg.sq[1].length, &sPsg.sq[1].on);
        LengthClock(&sPsg.wave.length, &sPsg.wave.on);
        LengthClock(&sPsg.noise.length, &sPsg.noise.on);
    }
    if (step == 2 || step == 6)
        SweepClock();
    if (step == 7)
    {
        EnvelopeClock(&sPsg.sq[0].env);
        EnvelopeClock(&sPsg.sq[1].env);
        EnvelopeClock(&sPsg.noise.env);
    }
    sPsg.seqStep = (uint8_t)((step + 1u) & 7u);
}

/* Runs the hardware for `cycles`, stopping at each sequencer step so its
 * clocks land between the right channel steps. */
static void Advance(uint32_t cycles)
{
    while (cycles != 0)
    {
        uint32_t span = cycles;

        if (sPsg.powered)
        {
            if (span > sPsg.seqTimer)
                span = sPsg.seqTimer;
            SquareRun(&sPsg.sq[0], span);
            SquareRun(&sPsg.sq[1], span);
            WaveRun(span);
            NoiseRun(span);
            sPsg.seqTimer -= span;
            if (sPsg.seqTimer == 0)
            {
                SequencerStep();
                sPsg.seqTimer = SEQ_CYCLES;
            }
        }
        sPsg.cycle += span;
        cycles -= span;
    }
}

static void PowerOff(void)
{
    uint32_t off;

    sPsg.powered = 0;
    memset(sPsg.sq, 0, sizeof sPsg.sq);
    memset(&sPsg.sweep, 0, sizeof sPsg.sweep);
    memset(&sPsg.noise, 0, sizeof sPsg.noise);
    sPsg.wave.on = 0;
    sPsg.wave.length.counter = 0;
    sPsg.wave.length.enabled = 0;
    sPsg.wave.nr30 = 0;
    sPsg.wave.nr32 = 0;
    sPsg.wave.freq = 0;
    sPsg.wave.timer = 0;
    sPsg.wave.pos = 0; /* wave RAM survives */
    sPsg.sq[0].nrx1Seen = NOT_SEEN;
    sPsg.sq[1].nrx1Seen = NOT_SEEN;
    sPsg.wave.nr31Seen = NOT_SEEN;
    sPsg.noise.nr41Seen = NOT_SEEN;
    sPsg.ch1FreqImage = 0;
    sPsg.nr50 = 0;
    sPsg.nr51 = 0;
    for (off = REG_PSG_FIRST; off < REG_PSG_END; off++)
        Wr(off, 0);
}

static void PowerOn(void)
{
    sPsg.powered = 1;
    sPsg.seqTimer = SEQ_CYCLES;
    sPsg.seqStep = 0;
}

/* The bits the hardware itself changes: channel 1's swept frequency and
 * the SOUNDCNT_X on flags (psg.h). */
static void Publish(void)
{
    uint16_t freq = sPsg.sq[0].freq;
    uint8_t status;

    if (sPsg.powered && freq != sPsg.ch1FreqImage)
    {
        Wr(REG_NR13, (uint8_t)(freq & 0xFFu));
        Wr(REG_NR14, (uint8_t)((Rd(REG_NR14) & ~NRX4_FREQ_HIGH) | (freq >> 8)));
        sPsg.ch1FreqImage = freq;
    }
    status = (uint8_t)(Rd(REG_NR52) & NR52_MASTER);
    status |= (uint8_t)(sPsg.sq[0].on ? 1u : 0u);
    status |= (uint8_t)(sPsg.sq[1].on ? 2u : 0u);
    status |= (uint8_t)(sPsg.wave.on ? 4u : 0u);
    status |= (uint8_t)(sPsg.noise.on ? 8u : 0u);
    Wr(REG_NR52, status);
}

/* --- Mixer DAC stage (OUTPUT, above) --------------------------------------- */

static int32_t PsgSide(const uint8_t levels[4], unsigned enables, unsigned volume)
{
    unsigned code = sPsg.soundcntH & CNTH_PSG_VOLUME;
    uint32_t sum = 0;
    unsigned ch;

    for (ch = 0; ch < 4; ch++)
        if ((enables >> ch) & 1u)
            sum += levels[ch];
    if (code == 3)
        code = 2; /* "Prohibited": plays as 100% (AMBIGUITIES) */
    return (int32_t)((sum * 8u * (volume + 1u)) >> (4u - code));
}

/* One PWM tick: bias, clip to 10 bits, cut to the resolution; returned in
 * 10-bit units. */
static int32_t DacTick(int32_t mix)
{
    unsigned shift = 1u + (sPsg.soundbias >> BIAS_RESOLUTION_SHIFT);
    int32_t v = mix + (int32_t)(sPsg.soundbias & BIAS_LEVEL);

    if (v < 0)
        v = 0;
    else if (v > 0x3FF)
        v = 0x3FF;
    return (int32_t)(((uint32_t)v >> shift) << shift);
}

static int16_t ClampS16(int32_t v)
{
    if (v > INT16_MAX)
        return INT16_MAX;
    if (v < INT16_MIN)
        return INT16_MIN;
    return (int16_t)v;
}

/* --- Interface ------------------------------------------------------------ */

void PortPsg_Init(volatile uint8_t *io)
{
    memset(&sPsg, 0, sizeof sPsg);
    sPsg.io = io;
    sPsg.sq[0].nrx1Seen = NOT_SEEN;
    sPsg.sq[1].nrx1Seen = NOT_SEEN;
    sPsg.wave.nr31Seen = NOT_SEEN;
    sPsg.noise.nr41Seen = NOT_SEEN;
    sPsg.soundbias = BIAS_BOOT;
}

void PortPsg_ResetIo(volatile uint8_t *io)
{
    uint32_t off;

    for (off = REG_PSG_FIRST; off < REG_FIFO_END; off++)
        io[off] = 0;
    io[REG_SOUNDBIAS] = (uint8_t)(BIAS_BOOT & 0xFFu);
    io[REG_SOUNDBIAS + 1] = (uint8_t)(BIAS_BOOT >> 8);
}

void PortPsg_Latch(void)
{
    if (sPsg.io == NULL)
        return;
    sPsg.soundcntH = Rd16(REG_SOUNDCNT_H);
    sPsg.soundbias = Rd16(REG_SOUNDBIAS);
    if (!(Rd(REG_NR52) & NR52_MASTER))
    {
        PowerOff();
        Publish();
        return;
    }
    if (!sPsg.powered)
        PowerOn();
    sPsg.sweep.nr10 = Rd(REG_NR10);
    if (ApplySquare(&sPsg.sq[0], &sCh1Regs))
        SweepTrigger();
    sPsg.ch1FreqImage = sPsg.sq[0].freq;
    ApplySquare(&sPsg.sq[1], &sCh2Regs);
    ApplyWave();
    ApplyNoise();
    sPsg.nr50 = Rd(REG_NR50);
    sPsg.nr51 = Rd(REG_NR51);
    Publish();
}

void PortPsg_Render(const int8_t *laneA, const int8_t *laneB, int16_t *lr, uint32_t samples)
{
    const unsigned res = sPsg.soundbias >> BIAS_RESOLUTION_SHIFT;
    const uint32_t period = 512u >> res; /* cycles per PWM tick */
    const int32_t bias = (int32_t)(sPsg.soundbias & BIAS_LEVEL);
    const uint16_t cnth = sPsg.soundcntH;
    uint32_t i;

    if (sPsg.io == NULL)
        return;
    for (i = 0; i < samples; i++)
    {
        int32_t fifoL = 0, fifoR = 0, accL = 0, accR = 0, n = 0;
        uint32_t left = PORT_PSG_SAMPLE_CYCLES;

        if (sPsg.powered)
        {
            int32_t a = laneA != NULL ? laneA[i] : 0;
            int32_t b = laneB != NULL ? laneB[i] : 0;

            a *= (cnth & CNTH_A_FULL) ? 4 : 2;
            b *= (cnth & CNTH_B_FULL) ? 4 : 2;
            fifoL = ((cnth & CNTH_A_LEFT) ? a : 0) + ((cnth & CNTH_B_LEFT) ? b : 0);
            fifoR = ((cnth & CNTH_A_RIGHT) ? a : 0) + ((cnth & CNTH_B_RIGHT) ? b : 0);
        }
        for (;;)
        {
            uint32_t toTick = period - (sPsg.cycle & (period - 1u));
            uint8_t levels[4];

            if (toTick > left)
            {
                Advance(left);
                break;
            }
            Advance(toTick);
            left -= toTick;
            levels[0] = SquareLevel(&sPsg.sq[0]);
            levels[1] = SquareLevel(&sPsg.sq[1]);
            levels[2] = WaveLevel();
            levels[3] = NoiseLevel();
            accL += DacTick(PsgSide(levels, sPsg.nr51 >> 4, (sPsg.nr50 >> 4) & 7u) + fifoL) - bias;
            accR += DacTick(PsgSide(levels, sPsg.nr51 & 0x0Fu, sPsg.nr50 & 7u) + fifoR) - bias;
            n++;
            sPsg.pwmTicks++;
        }
        lr[2 * i] = n != 0 ? ClampS16(accL * S16_PER_UNIT / n) : 0;
        lr[2 * i + 1] = n != 0 ? ClampS16(accR * S16_PER_UNIT / n) : 0;
    }
    Publish();
}

void PortPsg_Run(uint32_t cycles)
{
    Advance(cycles);
}

int PortPsg_ChannelOn(int ch)
{
    switch (ch)
    {
    case 1: return sPsg.sq[0].on;
    case 2: return sPsg.sq[1].on;
    case 3: return sPsg.wave.on;
    case 4: return sPsg.noise.on;
    default: return 0;
    }
}

uint8_t PortPsg_ChannelLevel(int ch)
{
    switch (ch)
    {
    case 1: return SquareLevel(&sPsg.sq[0]);
    case 2: return SquareLevel(&sPsg.sq[1]);
    case 3: return WaveLevel();
    case 4: return NoiseLevel();
    default: return 0;
    }
}
