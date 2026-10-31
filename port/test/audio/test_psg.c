/*
 * port/test/audio/test_psg.c: unit tests for port/audio/psg.c (checkpoint
 * P3.4b, audio-design.md 4.5), plus the register image rules of sound.md
 * section 5 and the mixer's DAC stage.
 *
 * Expected values come from GBATEK "GBA Sound Controller" (formulas quoted
 * at each test) and, where GBATEK is silent, Pan Docs "Audio Details"
 * (marked). Times are in system clock cycles (2^24 Hz), the model's
 * internal rate, so every period must match exactly.
 *
 * The test includes psg.c to reach its static units. Build and run:
 *   make -f port/test/audio/psg.mk
 */
#include "psg.c"

#include <stdio.h>
#include <stdlib.h>

static uint8_t gIo[0x400];
static int gChecks, gFails;

#define CHECK(cond, ...)                                       \
    do                                                         \
    {                                                          \
        gChecks++;                                             \
        if (!(cond))                                           \
        {                                                      \
            gFails++;                                          \
            printf("FAIL %s:%d: ", __func__, __LINE__);        \
            printf(__VA_ARGS__);                               \
            printf("\n");                                      \
        }                                                      \
    } while (0)

#define CHECK_EQ(got, want)                                                       \
    CHECK((long)(got) == (long)(want), "%s = %ld, want %s = %ld", #got, (long)(got), \
          #want, (long)(want))

/* GBATEK constants, in cycles of the 2^24 Hz system clock. */
#define HZ 16777216u
#define ENVELOPE_UNIT (HZ / 64u) /* "units of n/64s" */
#define SWEEP_UNIT (HZ / 128u)   /* "units of 7.8ms" = 1/128 s */
#define LENGTH_UNIT (HZ / 256u)  /* "units of (64-n)/256s" */
#define SEQ_STEP (HZ / 512u)     /* Pan Docs: 512 Hz frame sequencer */

static void W8(uint32_t off, uint8_t v)
{
    gIo[off] = v;
}

static void W16(uint32_t off, uint16_t v)
{
    gIo[off] = (uint8_t)v;
    gIo[off + 1] = (uint8_t)(v >> 8);
}

static uint16_t R16(uint32_t off)
{
    return (uint16_t)(gIo[off] | gIo[off + 1] << 8);
}

/* Cold boot, then what FE8's SoundInit and MPlayExtender leave
 * (m4a.c:241-254, 324-332): master on, NR50 0x77, SOUNDCNT_H 0xA90E,
 * SOUNDBIAS 0x4200. NR51 is set by each test. */
static void Boot(void)
{
    memset(gIo, 0xEE, sizeof gIo);
    PortPsg_ResetIo(gIo);
    PortPsg_Init(gIo);
    W8(REG_NR52, 0x8F);
    W8(REG_NR50, 0x77);
    W8(REG_NR51, 0x00);
    W16(REG_SOUNDCNT_H, 0xA90E);
    W8(REG_SOUNDBIAS + 1, (uint8_t)((gIo[REG_SOUNDBIAS + 1] & 0x3F) | 0x40));
}

/* Runs to one cycle before `at` (counted from the call) and checks that
 * `value()` has not changed yet, then one more cycle and checks it has. */
static long (*gProbe)(void);
static int ExpectChangeAt(uint32_t at, long *before, long *after)
{
    long v0 = gProbe();

    PortPsg_Run(at - 1);
    *before = gProbe();
    PortPsg_Run(1);
    *after = gProbe();
    return *before == v0 && *after != v0;
}

static long ProbeCh1Volume(void) { return sPsg.sq[0].env.volume; }
static long ProbeNoiseVolume(void) { return sPsg.noise.env.volume; }
static long ProbeCh1Freq(void) { return sPsg.sq[0].freq; }
static long ProbeCh1On(void) { return PortPsg_ChannelOn(1); }
static long ProbeCh2On(void) { return PortPsg_ChannelOn(2); }
static long ProbeCh3On(void) { return PortPsg_ChannelOn(3); }
static long ProbeCh4On(void) { return PortPsg_ChannelOn(4); }

/* --- Duty ----------------------------------------------------------------- */

/* GBATEK "Wave Pattern Duty": 0: 12.5% ( -_______ ), 1: 25% ( --______ ),
 * 2: 50% ( ----____ ), 3: 75% ( ------__ ). One step is 1/8 of the period
 * of "Frequency = 131072/(2048-n)Hz": (2048-n) * 16 cycles. */
static void TestDuty(void)
{
    static const char *const want[4] = { "-_______", "--______", "----____", "------__" };
    unsigned duty, step;

    for (duty = 0; duty < 4; duty++)
    {
        char got[9];

        Boot();
        W8(REG_NR11, (uint8_t)(duty << 6));
        W8(REG_NR12, 0xF0); /* volume 15, no envelope */
        W8(REG_NR13, 0x00);
        W8(REG_NR14, 0x84); /* trigger, n = 0x400 */
        PortPsg_Latch();
        for (step = 0; step < 8; step++)
        {
            got[step] = PortPsg_ChannelLevel(1) == 15 ? '-' : (PortPsg_ChannelLevel(1) == 0 ? '_' : '?');
            PortPsg_Run((2048u - 0x400u) * 16u);
        }
        got[8] = 0;
        CHECK(strcmp(got, want[duty]) == 0, "duty %u: %s, want %s", duty, got, want[duty]);
    }
}

/* Cycles between two rising edges of channel `ch`'s output. */
static uint32_t RisingPeriod(int ch, uint32_t limit)
{
    uint32_t t = 0, first = 0;
    int edges = 0;
    uint8_t prev = PortPsg_ChannelLevel(ch);

    while (t < limit && edges < 2)
    {
        uint8_t now;

        PortPsg_Run(1);
        t++;
        now = PortPsg_ChannelLevel(ch);
        if (now > prev)
        {
            if (edges++ == 0)
                first = t;
        }
        prev = now;
    }
    return edges == 2 ? t - first : 0;
}

/* GBATEK channels 1-2 "Frequency = 131072/(2048-n)Hz": a period of
 * 2^24 / (131072 / (2048-n)) = (2048-n) * 128 cycles. */
static void TestSquareFrequency(void)
{
    static const uint16_t ns[] = { 0, 1, 1024, 1792, 1985, 2046, 2047 };
    unsigned i;

    for (i = 0; i < sizeof ns / sizeof ns[0]; i++)
    {
        uint16_t n = ns[i];

        Boot();
        W8(REG_NR21, 0x80); /* 50% */
        W8(REG_NR22, 0xF0);
        W8(REG_NR23, (uint8_t)n);
        W8(REG_NR24, (uint8_t)(0x80 | n >> 8));
        PortPsg_Latch();
        CHECK_EQ(RisingPeriod(2, 4u * 262144u), (2048u - n) * 128u);
    }
}

/* --- Envelope ------------------------------------------------------------- */

/* GBATEK NRx2: "Envelope Step-Time; units of n/64s (1-7, 0=No Envelope)",
 * "Envelope Direction (0=Decrease, 1=Increase)", "Initial Volume of
 * envelope (1-15, 0=No Sound)". The trigger lands on the sequencer's start,
 * so step k comes after exactly k * n/64 s. */
static void TestEnvelope(void)
{
    long before, after;
    unsigned k;

    /* Decrease from 15, n = 3: 15 steps of 3/64 s, then it stays at 0. */
    Boot();
    W8(REG_NR11, 0xC0);
    W8(REG_NR12, 0xF3);
    W8(REG_NR14, 0x87);
    PortPsg_Latch();
    gProbe = ProbeCh1Volume;
    for (k = 1; k <= 15; k++)
    {
        CHECK(ExpectChangeAt(3u * ENVELOPE_UNIT, &before, &after), "step %u: %ld -> %ld", k, before, after);
        CHECK_EQ(after, 15 - (long)k);
    }
    PortPsg_Run(8u * ENVELOPE_UNIT);
    CHECK_EQ(sPsg.sq[0].env.volume, 0);
    CHECK_EQ(PortPsg_ChannelOn(1), 1); /* volume 0 does not stop the channel */

    /* Increase from 0, n = 1: one step every 1/64 s up to 15. */
    Boot();
    W8(REG_NR12, 0x09);
    W8(REG_NR14, 0x80);
    PortPsg_Latch();
    for (k = 1; k <= 15; k++)
    {
        CHECK(ExpectChangeAt(ENVELOPE_UNIT, &before, &after), "up %u: %ld -> %ld", k, before, after);
        CHECK_EQ(after, (long)k);
    }
    PortPsg_Run(4u * ENVELOPE_UNIT);
    CHECK_EQ(sPsg.sq[0].env.volume, 15);

    /* n = 0: no envelope. */
    Boot();
    W8(REG_NR12, 0xA8);
    W8(REG_NR14, 0x80);
    PortPsg_Latch();
    PortPsg_Run(HZ);
    CHECK_EQ(sPsg.sq[0].env.volume, 10);

    /* Channel 4 shares the rule: n = 7 is 7/64 s. */
    Boot();
    W8(REG_NR42, 0x57);
    W8(REG_NR44, 0x80);
    PortPsg_Latch();
    gProbe = ProbeNoiseVolume;
    CHECK(ExpectChangeAt(7u * ENVELOPE_UNIT, &before, &after), "noise: %ld -> %ld", before, after);
    CHECK_EQ(after, 4);

    /* NRx2 bits 3-7 zero switch the DAC off, which stops the channel
     * (Pan Docs; CgbOscOff writes 8 instead, which keeps the DAC on). */
    Boot();
    W8(REG_NR12, 0xF0);
    W8(REG_NR14, 0x80);
    PortPsg_Latch();
    CHECK_EQ(PortPsg_ChannelOn(1), 1);
    W8(REG_NR12, 0x08);
    W8(REG_NR14, 0x80); /* CgbOscOff(1) */
    PortPsg_Latch();
    CHECK_EQ(PortPsg_ChannelOn(1), 1);
    CHECK_EQ(sPsg.sq[0].env.volume, 0);
    W8(REG_NR12, 0x00);
    PortPsg_Latch();
    CHECK_EQ(PortPsg_ChannelOn(1), 0);
    W8(REG_NR14, 0x80);
    PortPsg_Latch();
    CHECK_EQ(PortPsg_ChannelOn(1), 0); /* a trigger with the DAC off does nothing */
}

/* --- Sweep ---------------------------------------------------------------- */

/* GBATEK NR10: "X(t) = X(t-1) +/- X(t-1)/2^n", "Sweep Time; units of 7.8ms"
 * (1/128 s). Sweep clocks are sequencer steps 2 and 6 (Pan Docs): from a
 * trigger at the sequencer's start, the k-th clock is at
 * 3 * SEQ_STEP + (k - 1) * SWEEP_UNIT. */
static void TestSweep(void)
{
    long before, after;
    uint32_t x, next;
    unsigned k;
    int16_t lr[2 * PORT_PSG_FRAME_SAMPLES];

    /* FE8's voice_square_1 sweep 53 = 0x35: time 3, increase, shift 5. */
    Boot();
    W8(REG_NR10, 0x35);
    W8(REG_NR11, 0x80);
    W8(REG_NR12, 0xF0);
    W8(REG_NR13, 1000 & 0xFF);
    W8(REG_NR14, 0x80 | 1000 >> 8);
    PortPsg_Latch();
    gProbe = ProbeCh1Freq;
    x = 1000;
    for (k = 1;; k++)
    {
        uint32_t at = k == 1 ? 3u * SEQ_STEP + 2u * SWEEP_UNIT : 3u * SWEEP_UNIT;

        next = x + (x >> 5);
        CHECK(ExpectChangeAt(at, &before, &after), "sweep step %u: %ld -> %ld", k, before, after);
        CHECK_EQ(after, (long)next);
        x = next;
        if (x + (x >> 5) > 2047)
            break; /* Pan Docs: the check after the write stops the channel */
        CHECK_EQ(PortPsg_ChannelOn(1), 1);
    }
    CHECK_EQ(x, 2016);
    CHECK_EQ(PortPsg_ChannelOn(1), 0);

    /* Write-back: the image's NR13/NR14 frequency follows the sweep, NR14's
     * other bits and NR10 stay as written. */
    Boot();
    W8(REG_NR10, 0x1B); /* FE8's sweep 27: time 1, decrease, shift 3 */
    W8(REG_NR12, 0xF0);
    W8(REG_NR13, 1000 & 0xFF);
    W8(REG_NR14, 0xC0 | 1000 >> 8);
    W8(REG_NR11, 0x00); /* length 64 ticks, enabled: 1/4 s */
    PortPsg_Latch();
    gProbe = ProbeCh1Freq;
    x = 1000;
    for (k = 1; k <= 6; k++)
    {
        uint32_t at = k == 1 ? 3u * SEQ_STEP : SWEEP_UNIT;

        next = x - (x >> 3);
        CHECK(ExpectChangeAt(at, &before, &after), "down %u: %ld -> %ld", k, before, after);
        CHECK_EQ(after, (long)next);
        x = next;
    }
    PortPsg_Render(NULL, NULL, lr, 1);
    CHECK_EQ(gIo[REG_NR13] | (gIo[REG_NR14] & 7) << 8, sPsg.sq[0].freq);
    CHECK_EQ(gIo[REG_NR14] & 0xF8, 0x40);
    CHECK_EQ(gIo[REG_NR10], 0x1B);
    /* The next latch keeps the swept frequency. */
    PortPsg_Latch();
    CHECK_EQ(sPsg.sq[0].freq, (long)(gIo[REG_NR13] | (gIo[REG_NR14] & 7) << 8));
    CHECK(sPsg.sq[0].freq < 1000, "freq %u", sPsg.sq[0].freq);

    /* Overflow at the trigger (Pan Docs): shift 1, increase, 1400 + 700 >
     * 2047 stops the channel at once, even with sweep time 0. GBATEK: "Sweep
     * is disabled by setting Sweep Time to zero, if so, the direction bit
     * should be set" - with it set, the channel plays. */
    Boot();
    W8(REG_NR10, 0x01);
    W8(REG_NR12, 0xF0);
    W8(REG_NR13, 1400 & 0xFF);
    W8(REG_NR14, 0x80 | 1400 >> 8);
    PortPsg_Latch();
    CHECK_EQ(PortPsg_ChannelOn(1), 0);
    W8(REG_NR10, 0x09);
    W8(REG_NR14, 0x80 | 1400 >> 8);
    PortPsg_Latch();
    CHECK_EQ(PortPsg_ChannelOn(1), 1);
    PortPsg_Run(HZ);
    CHECK_EQ(sPsg.sq[0].freq, 1400);

    /* Time 0, shift 0 (FE8's default NR10 = 0): no change. */
    Boot();
    W8(REG_NR12, 0xF0);
    W8(REG_NR13, 0x34);
    W8(REG_NR14, 0x85);
    PortPsg_Latch();
    PortPsg_Run(HZ);
    CHECK_EQ(sPsg.sq[0].freq, 0x534);
    CHECK_EQ(PortPsg_ChannelOn(1), 1);
}

/* --- Length --------------------------------------------------------------- */

/* GBATEK: "Sound length; units of (64-n)/256s" (channels 1, 2, 4) and
 * "(256-n)/256s" (channel 3), with "Length Flag (1=Stop output when length
 * in NRx1 expires)". Length clocks are sequencer steps 0, 2, 4, 6; the
 * first comes 1/512 s after the sequencer starts, so a channel triggered at
 * the start stops after 1/512 s + (L - 1)/256 s, L = 64 - n (256 - n). */
static uint32_t LengthOffAt(uint32_t ticks)
{
    return SEQ_STEP + (ticks - 1u) * LENGTH_UNIT;
}

static void TestLength(void)
{
    static const uint8_t ns[] = { 0, 1, 32, 60, 63 };
    long before, after;
    unsigned i;
    int16_t lr[2];

    for (i = 0; i < sizeof ns / sizeof ns[0]; i++)
    {
        Boot();
        W8(REG_NR11, (uint8_t)(0x80 | ns[i]));
        W8(REG_NR12, 0xF0);
        W8(REG_NR14, 0xC4); /* trigger, length flag */
        PortPsg_Latch();
        gProbe = ProbeCh1On;
        CHECK(ExpectChangeAt(LengthOffAt(64u - ns[i]), &before, &after), "ch1 n=%u", ns[i]);
    }

    /* Channel 2, and the status bit in SOUNDCNT_X. */
    Boot();
    W8(REG_NR21, 0x3F);
    W8(REG_NR22, 0xF0);
    W8(REG_NR24, 0xC0);
    PortPsg_Latch();
    CHECK_EQ(gIo[REG_NR52], 0x82);
    gProbe = ProbeCh2On;
    CHECK(ExpectChangeAt(LengthOffAt(1), &before, &after), "ch2 n=63");
    PortPsg_Render(NULL, NULL, lr, 1);
    CHECK_EQ(gIo[REG_NR52], 0x80);
    /* Retrigger with no NR21 write: the run-out counter reloads with 64
     * (Pan Docs). The sequencer is now just past step 0, so the next length
     * clock is 2 steps away. */
    W8(REG_NR24, 0xC0);
    PortPsg_Latch();
    CHECK_EQ(PortPsg_ChannelOn(2), 1);
    CHECK_EQ(sPsg.sq[1].length.counter, 64);

    /* Channel 3: 256 - n. */
    Boot();
    W8(REG_NR30, 0x80);
    W8(REG_NR31, 0);
    W8(REG_NR34, 0xC0);
    PortPsg_Latch();
    gProbe = ProbeCh3On;
    CHECK(ExpectChangeAt(LengthOffAt(256), &before, &after), "ch3 n=0");
    Boot();
    W8(REG_NR30, 0x80);
    W8(REG_NR31, 255);
    W8(REG_NR34, 0xC0);
    PortPsg_Latch();
    CHECK(ExpectChangeAt(LengthOffAt(1), &before, &after), "ch3 n=255");

    /* Channel 4. */
    Boot();
    W8(REG_NR41, 50);
    W8(REG_NR42, 0xF0);
    W8(REG_NR44, 0xC0);
    PortPsg_Latch();
    gProbe = ProbeCh4On;
    CHECK(ExpectChangeAt(LengthOffAt(14), &before, &after), "ch4 n=50");

    /* Length flag clear: plays on. */
    Boot();
    W8(REG_NR11, 0x3F);
    W8(REG_NR12, 0xF0);
    W8(REG_NR14, 0x80);
    PortPsg_Latch();
    PortPsg_Run(HZ);
    CHECK_EQ(PortPsg_ChannelOn(1), 1);
}

/* --- Wave RAM ------------------------------------------------------------- */

/* GBATEK channel 3: "Sample Rate; 2097152/(2048-n) Hz" ((2048-n) * 8
 * cycles a sample), 32 4-bit samples a bank, "MSBs of 1st byte, followed by
 * LSBs of 1st byte, followed by MSBs of 2nd byte", tone "65536/(2048-n)
 * Hz" (32 samples). NR32: "Sound Volume (0=Mute/Zero, 1=100%, 2=50%,
 * 3=25%)", "Force Volume (1=Force 75% regardless of above Sound Volume)". */
static const uint8_t kRamp[16] = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
                                   0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10 };

static void WriteWindow(const uint8_t bytes[16])
{
    memcpy(&gIo[REG_WAVE_RAM], bytes, 16);
}

/* CgbSound's channel 3 note start (m4a.c:929-938, then 1150-1155). */
static void Fe8WaveNote(const uint8_t bytes[16], uint8_t nr32, uint16_t n)
{
    W8(REG_NR30, 0x40);
    WriteWindow(bytes);
    W8(REG_NR30, 0x00);
    W8(REG_NR31, 0);
    W8(REG_NR33, (uint8_t)n);
    W8(REG_NR34, (uint8_t)(0x80 | n >> 8));
    W8(REG_NR32, nr32);
    W8(REG_NR30, 0x80);
    W8(REG_NR34, (uint8_t)(0x80 | n >> 8));
}

static unsigned RampSample(unsigned i)
{
    uint8_t byte = kRamp[(i & 31u) >> 1];
    return (i & 1u) ? byte & 0x0Fu : byte >> 4;
}

static void TestWave(void)
{
    static const struct
    {
        uint8_t nr32;
        unsigned num, shift; /* level = sample * num >> shift */
    } volumes[] = {
        { 0x20, 1, 0 }, { 0x40, 1, 1 }, { 0x60, 1, 2 }, { 0x00, 0, 0 },
        { 0x80, 3, 2 }, { 0xE0, 3, 2 },
    };
    const uint16_t n = 1792; /* (2048 - 1792) * 8 = 2048 cycles a sample */
    unsigned v, i;
    uint8_t aa[16], bb[16];

    for (v = 0; v < sizeof volumes / sizeof volumes[0]; v++)
    {
        int ok = 1;

        Boot();
        Fe8WaveNote(kRamp, volumes[v].nr32, n);
        PortPsg_Latch();
        CHECK_EQ(PortPsg_ChannelOn(3), 1);
        for (i = 0; i < 70; i++)
        {
            unsigned want = (RampSample(i) * volumes[v].num) >> volumes[v].shift;

            if (PortPsg_ChannelLevel(3) != want)
            {
                if (ok)
                    printf("  nr32 %02X sample %u: %u, want %u\n", volumes[v].nr32, i,
                           PortPsg_ChannelLevel(3), want);
                ok = 0;
            }
            PortPsg_Run((2048u - n) * 8u);
        }
        CHECK(ok, "wave samples at NR32 = %02X", volumes[v].nr32);
    }

    /* Tone period: the 32-sample wave repeats every (2048-n) * 256 cycles
     * = 2^24 / (65536 / (2048-n)). A wave whose only non-zero sample is
     * sample 0 has one rising edge a period. */
    {
        static const uint16_t ns[] = { 0, 1000, 2000, 2047 };
        uint8_t pulse[16] = { 0xF0 };

        for (i = 0; i < sizeof ns / sizeof ns[0]; i++)
        {
            Boot();
            Fe8WaveNote(pulse, 0x20, ns[i]);
            PortPsg_Latch();
            CHECK_EQ(RisingPeriod(3, 3u * 524288u), (2048u - ns[i]) * 256u);
        }
    }

    /* Banks. m4a's sequence above put the ramp in bank 0, the bank that
     * plays (GBATEK: writes go to the bank not selected at the time). */
    Boot();
    Fe8WaveNote(kRamp, 0x20, n);
    PortPsg_Latch();
    CHECK(memcmp(sPsg.wave.bank[0], kRamp, 16) == 0, "ramp in bank 0");

    /* Two banks: load bank 1 with 0x55.. (window changed while bank 1 is
     * selected), then bank 0 with 0xAA.., then play both from bank 0:
     * "output will start by replaying the currently selected bank". */
    memset(aa, 0xAA, sizeof aa);
    memset(bb, 0x55, sizeof bb);
    Boot();
    W8(REG_NR30, 0x40);
    WriteWindow(bb);
    PortPsg_Latch();
    W8(REG_NR30, 0x00);
    WriteWindow(aa);
    PortPsg_Latch();
    CHECK(memcmp(sPsg.wave.bank[0], aa, 16) == 0 && memcmp(sPsg.wave.bank[1], bb, 16) == 0, "banks loaded");
    /* An unchanged window does not copy again when the selection changes. */
    W8(REG_NR30, 0x40);
    PortPsg_Latch();
    CHECK(memcmp(sPsg.wave.bank[1], bb, 16) == 0, "bank 1 kept");
    W8(REG_NR30, 0xA0); /* two banks, bank 0 first, playback */
    W8(REG_NR32, 0x20);
    W8(REG_NR33, (uint8_t)n);
    W8(REG_NR34, (uint8_t)(0x80 | n >> 8));
    PortPsg_Latch();
    {
        int ok = 1;

        for (i = 0; i < 130; i++)
        {
            unsigned want = (i % 64u) < 32u ? 0xA : 0x5;

            ok &= PortPsg_ChannelLevel(3) == want;
            PortPsg_Run((2048u - n) * 8u);
        }
        CHECK(ok, "two banks play 32 x A then 32 x 5");
    }
    W8(REG_NR30, 0xE0); /* bank 1 first */
    W8(REG_NR34, (uint8_t)(0x80 | n >> 8));
    PortPsg_Latch();
    CHECK_EQ(PortPsg_ChannelLevel(3), 0x5);

    /* NR30 bit 7 clear: "Sound Channel 3 Off (0=Stop, 1=Playback)". */
    W8(REG_NR30, 0x00);
    PortPsg_Latch();
    CHECK_EQ(PortPsg_ChannelOn(3), 0);
    W8(REG_NR34, 0x80);
    PortPsg_Latch();
    CHECK_EQ(PortPsg_ChannelOn(3), 0);
}

/* --- Noise ---------------------------------------------------------------- */

/* GBATEK channel 4: "X=X SHR 1, IF carry THEN Out=HIGH, X=X XOR 60h (7bit)
 * / 6000h (15bit) ELSE Out=LOW", starting from "X=40h (7bit) or X=4000h
 * (15bit)". Values below worked by hand from that rule. */
static void TestLfsr(void)
{
    static const uint16_t states7[20] = { 0x20, 0x10, 0x08, 0x04, 0x02, 0x01, 0x60, 0x30, 0x18, 0x0C,
                                          0x06, 0x03, 0x61, 0x50, 0x28, 0x14, 0x0A, 0x05, 0x62, 0x31 };
    static const char outs7[] = "0000001000001100001010001111001000101100";
    static const char outs15[] = "0000000000000010000000000000110000000000";
    uint16_t x;
    unsigned i, ones;

    x = LfsrSeed(1);
    CHECK_EQ(x, 0x40);
    for (i = 0; i < 40; i++)
    {
        unsigned out = x & 1u;

        x = LfsrClock(x, 1);
        CHECK(out == (unsigned)(outs7[i] - '0'), "7-bit out %u", i);
        if (i < 20)
            CHECK(x == states7[i], "7-bit state %u: %04X, want %04X", i, x, states7[i]);
    }
    x = LfsrSeed(0);
    CHECK_EQ(x, 0x4000);
    for (i = 0; i < 40; i++)
    {
        unsigned out = x & 1u;

        x = LfsrClock(x, 0);
        CHECK(out == (unsigned)(outs15[i] - '0'), "15-bit out %u", i);
        if (i == 13)
            CHECK_EQ(x, 0x0001);
        if (i == 14)
            CHECK_EQ(x, 0x6000);
    }

    /* Both are maximal-length: periods 127 and 32767, with 64 and 16384
     * HIGH outputs per period. */
    x = LfsrSeed(1);
    ones = 0;
    for (i = 1;; i++)
    {
        ones += x & 1u;
        x = LfsrClock(x, 1);
        if (x == 0x40 || i > 200)
            break;
    }
    CHECK_EQ(i, 127);
    CHECK_EQ(ones, 64);
    x = LfsrSeed(0);
    ones = 0;
    for (i = 1;; i++)
    {
        ones += x & 1u;
        x = LfsrClock(x, 0);
        if (x == 0x4000 || i > 40000)
            break;
    }
    CHECK_EQ(i, 32767);
    CHECK_EQ(ones, 16384);
}

/* GBATEK NR43: "Frequency = 524288 Hz / r / 2^(s+1) ;For r=0 assume r=0.5
 * instead": 2^24 / that = 32 * r * 2^(s+1) cycles a clock. */
static void TestNoise(void)
{
    static const struct
    {
        uint8_t r, s, sevenBit;
    } cases[] = { { 0, 0, 1 }, { 1, 0, 0 }, { 2, 3, 1 }, { 5, 7, 0 }, { 7, 13, 1 }, { 0, 15, 0 } };
    unsigned i, k;

    for (i = 0; i < sizeof cases / sizeof cases[0]; i++)
    {
        uint8_t nr43 = (uint8_t)(cases[i].s << 4 | (cases[i].sevenBit ? 8 : 0) | cases[i].r);
        uint32_t want = (cases[i].r == 0 ? 16u : 32u * cases[i].r) << (cases[i].s + 1u);
        uint16_t x;

        CHECK(NoisePeriod(nr43) == want, "r=%u s=%u: %u, want %u", cases[i].r, cases[i].s, NoisePeriod(nr43), want);
        Boot();
        W8(REG_NR42, 0xF0);
        W8(REG_NR43, nr43);
        W8(REG_NR44, 0x80);
        PortPsg_Latch();
        x = LfsrSeed(cases[i].sevenBit);
        CHECK_EQ(sPsg.noise.lfsr, x);
        if (want > (1u << 20))
            continue; /* long periods: the formula check above is enough */
        for (k = 1; k <= 9; k++)
        {
            PortPsg_Run(want - 1);
            CHECK(sPsg.noise.lfsr == x, "r=%u s=%u clock %u early", cases[i].r, cases[i].s, k);
            PortPsg_Run(1);
            x = LfsrClock(x, cases[i].sevenBit);
            CHECK(sPsg.noise.lfsr == x, "r=%u s=%u clock %u", cases[i].r, cases[i].s, k);
            /* Out=HIGH plays the envelope volume, LOW plays 0. */
            CHECK_EQ(PortPsg_ChannelLevel(4), sPsg.noise.high ? 15 : 0);
        }
    }

    /* 7-bit, r = 1, s = 0: the 7th clock is the first HIGH. */
    Boot();
    W8(REG_NR42, 0xC0);
    W8(REG_NR43, 0x09);
    W8(REG_NR44, 0x80);
    PortPsg_Latch();
    PortPsg_Run(6u * 64u);
    CHECK_EQ(PortPsg_ChannelLevel(4), 0);
    PortPsg_Run(64u);
    CHECK_EQ(PortPsg_ChannelLevel(4), 12);
    PortPsg_Run(64u);
    CHECK_EQ(PortPsg_ChannelLevel(4), 0);
}

/* --- Register image ------------------------------------------------------- */

/* sound.md section 5: NR10, NR12/22/42, NR43 and NR51 must read back the
 * last value written (CgbSound reads them, m4a.c:911, 1136, 1144, 1160),
 * and so must SOUNDBIAS_H (m4a.c:1119). NRx4 bit 7 is write-only (GBATEK
 * "Initial ... (W)"). SOUNDCNT_X bits 0-3 are "Sound 1-4 ON flag (Read
 * Only)". */
static void TestReadback(void)
{
    static const struct
    {
        uint32_t off;
        uint8_t value;
    } kept[] = {
        { REG_NR10, 0x2D }, { REG_NR12, 0xA3 }, { REG_NR22, 0x5B }, { REG_NR42, 0x71 },
        { REG_NR43, 0x2C }, { REG_NR51, 0x9A }, { REG_NR11, 0x87 }, { REG_NR21, 0x45 },
        { REG_NR30, 0x80 }, { REG_NR31, 0x10 }, { REG_NR32, 0x40 }, { REG_NR41, 0x22 },
        { REG_NR50, 0x77 }, { REG_NR23, 0x99 }, { REG_NR33, 0x55 },
    };
    int16_t lr[2 * PORT_PSG_FRAME_SAMPLES];
    unsigned i, frame;

    Boot();
    for (i = 0; i < sizeof kept / sizeof kept[0]; i++)
        W8(kept[i].off, kept[i].value);
    W8(REG_NR13, 0x00);
    W8(REG_NR14, 0x86);
    W8(REG_NR24, 0x85);
    W8(REG_NR34, 0x83);
    W8(REG_NR44, 0x80);
    CHECK_EQ(gIo[REG_SOUNDBIAS + 1], 0x42);
    for (frame = 0; frame < 30; frame++)
    {
        PortPsg_Latch();
        if (frame == 0)
        {
            CHECK_EQ(gIo[REG_NR14], 0x06);
            CHECK_EQ(gIo[REG_NR24], 0x05);
            CHECK_EQ(gIo[REG_NR34], 0x03);
            CHECK_EQ(gIo[REG_NR44], 0x00);
            CHECK_EQ(gIo[REG_NR52], 0x8F);
        }
        PortPsg_Render(NULL, NULL, lr, PORT_PSG_FRAME_SAMPLES);
    }
    for (i = 0; i < sizeof kept / sizeof kept[0]; i++)
        CHECK(gIo[kept[i].off] == kept[i].value, "reg %03X = %02X, want %02X", kept[i].off, gIo[kept[i].off],
              kept[i].value);
    CHECK_EQ(R16(REG_SOUNDBIAS), 0x4200);
    CHECK_EQ(R16(REG_SOUNDCNT_H), 0xA90E);
    /* NR10 0x2D sweeps channel 1 down; NR13/NR14 bits 0-2 carry it. */
    CHECK_EQ(gIo[REG_NR13] | (gIo[REG_NR14] & 7) << 8, sPsg.sq[0].freq);
    CHECK(sPsg.sq[0].freq < 0x600, "swept to %03X", sPsg.sq[0].freq);
    CHECK_EQ(gIo[REG_NR14] & 0xF8, 0x00);
    CHECK_EQ(gIo[REG_NR52] & 0x70, 0);
    CHECK_EQ(gIo[REG_NR52] & 0x80, 0x80);
    CHECK_EQ(gIo[REG_NR52] & 0x0F, PortPsg_ChannelOn(1) | PortPsg_ChannelOn(2) << 1 | PortPsg_ChannelOn(3) << 2 |
                                       PortPsg_ChannelOn(4) << 3);
    CHECK_EQ(gIo[REG_NR52] & 0x0F, 0x0F);

    /* Wave RAM, SOUNDCNT_H and SOUNDBIAS are never written. */
    WriteWindow(kRamp);
    PortPsg_Latch();
    PortPsg_Render(NULL, NULL, lr, PORT_PSG_FRAME_SAMPLES);
    CHECK(memcmp(&gIo[REG_WAVE_RAM], kRamp, 16) == 0, "wave RAM image untouched");
}

/* A trigger is consumed once: the next latch without a new write does not
 * restart the envelope. */
static void TestTrigger(void)
{
    Boot();
    W8(REG_NR12, 0xF1);
    W8(REG_NR14, 0x80);
    PortPsg_Latch();
    CHECK_EQ(gIo[REG_NR14], 0x00);
    PortPsg_Run(2u * ENVELOPE_UNIT);
    CHECK_EQ(sPsg.sq[0].env.volume, 13);
    PortPsg_Latch();
    CHECK_EQ(sPsg.sq[0].env.volume, 13);
    W8(REG_NR14, 0x80);
    W8(REG_NR14, 0x80); /* CgbSound's double write for channel 1 */
    PortPsg_Latch();
    CHECK_EQ(sPsg.sq[0].env.volume, 15);
}

/* GBATEK SOUNDCNT_X bit 7: while clear, "all PSG registers at
 * 4000060h..4000081h are reset to zero". */
static void TestMaster(void)
{
    int16_t lr[2 * 8];
    int8_t lane[8];
    unsigned i;
    int ok = 1;

    Boot();
    W8(REG_NR51, 0xFF);
    W8(REG_NR12, 0xF0);
    W8(REG_NR14, 0x80);
    WriteWindow(kRamp);
    PortPsg_Latch();
    CHECK_EQ(PortPsg_ChannelOn(1), 1);
    W8(REG_NR52, 0x00);
    PortPsg_Latch();
    CHECK_EQ(PortPsg_ChannelOn(1), 0);
    for (i = REG_NR10; i <= REG_NR51; i++)
        ok &= gIo[i] == 0;
    CHECK(ok, "4000060h-4000081h zero");
    CHECK(memcmp(&gIo[REG_WAVE_RAM], kRamp, 16) == 0, "wave RAM kept");
    CHECK_EQ(R16(REG_SOUNDBIAS), 0x4200);
    CHECK_EQ(gIo[REG_NR52], 0x00);
    memset(lane, 100, sizeof lane);
    PortPsg_Render(lane, lane, lr, 8);
    ok = 1;
    for (i = 0; i < 16; i++)
        ok &= lr[i] == 0;
    CHECK(ok, "silent while disabled");
    /* Writes while disabled are discarded; after enabling, channels stay
     * off until triggered, and the sequencer starts over. */
    W8(REG_NR12, 0xF0);
    PortPsg_Latch();
    CHECK_EQ(gIo[REG_NR12], 0);
    W8(REG_NR52, 0x80);
    PortPsg_Latch();
    CHECK_EQ(PortPsg_ChannelOn(1), 0);
    CHECK_EQ(sPsg.seqStep, 0);
    CHECK_EQ(sPsg.seqTimer, SEQ_STEP);
}

/* --- Mixer and SOUNDBIAS --------------------------------------------------- */

/* GBATEK "Max Output Levels": each FIFO spans +/-200h (s * 4 at 100%, s * 2
 * at 50%), each PSG channel a quarter of that (level * 10h at full volume);
 * "The BIAS value is added to that signed value", the sum is "clipped to
 * MinMax(0,3FFh)", then cut to the resolution of SOUNDBIAS bits 14-15
 * (9, 8, 7, 6 bits). Output = (DAC value - bias) * 64. */
static int16_t RefOut(int32_t mix, uint16_t soundbias)
{
    int32_t bias = soundbias & 0x3FE;
    unsigned shift = 1u + (soundbias >> 14);
    int32_t v = mix + bias;

    v = v < 0 ? 0 : (v > 0x3FF ? 0x3FF : v);
    v = (v >> shift) << shift;
    return (int16_t)((v - bias) * 64 > 32767 ? 32767 : ((v - bias) * 64 < -32768 ? -32768 : (v - bias) * 64));
}

/* Channel 3 playing a constant level 15 (wave RAM all FFh at 100%). */
static void ConstantWave(void)
{
    uint8_t ff[16];

    memset(ff, 0xFF, sizeof ff);
    Fe8WaveNote(ff, 0x20, 1000);
    PortPsg_Latch();
}

static int AllFrames(const int16_t *lr, unsigned frames, int16_t left, int16_t right)
{
    unsigned i;

    for (i = 0; i < frames; i++)
        if (lr[2 * i] != left || lr[2 * i + 1] != right)
        {
            printf("  frame %u: %d,%d want %d,%d\n", i, lr[2 * i], lr[2 * i + 1], left, right);
            return 0;
        }
    return 1;
}

static void TestMix(void)
{
    int16_t lr[2 * PORT_PSG_FRAME_SAMPLES];
    int8_t a[PORT_PSG_FRAME_SAMPLES], b[PORT_PSG_FRAME_SAMPLES];
    unsigned i;
    int ok;

    /* FE8's set-up, DirectSound alone: A to the right and B to the left at
     * 100% (SOUNDCNT_H 0xA90E), bias 200h, 8 bits. Every s8 comes out as
     * s << 8 (audio-design 1.4). */
    Boot();
    PortPsg_Latch();
    for (i = 0; i < 2 * PORT_PSG_FRAME_SAMPLES; i++)
    {
        int s = (int)(i % 256u) - 128;

        if (i % PORT_PSG_FRAME_SAMPLES == 0 && i != 0)
        {
            PortPsg_Render(a, b, lr, PORT_PSG_FRAME_SAMPLES);
            ok = 1;
            for (unsigned j = 0; j < PORT_PSG_FRAME_SAMPLES; j++)
                ok &= lr[2 * j + 1] == a[j] * 256 && lr[2 * j] == b[j] * 256;
            CHECK(ok, "DirectSound s << 8, frame %u", i / PORT_PSG_FRAME_SAMPLES);
        }
        a[i % PORT_PSG_FRAME_SAMPLES] = (int8_t)s;
        b[i % PORT_PSG_FRAME_SAMPLES] = (int8_t)(-1 - s);
    }
    PortPsg_Render(a, b, lr, PORT_PSG_FRAME_SAMPLES);
    ok = 1;
    for (i = 0; i < PORT_PSG_FRAME_SAMPLES; i++)
        ok &= lr[2 * i + 1] == a[i] * 256 && lr[2 * i] == b[i] * 256;
    CHECK(ok, "DirectSound s << 8, last frame");
    /* A FIFO at 50% (SOUNDCNT_H bit 2 clear): s * 2, cut to 8 bits. */
    W16(REG_SOUNDCNT_H, 0xA90A);
    PortPsg_Latch();
    PortPsg_Render(a, b, lr, PORT_PSG_FRAME_SAMPLES);
    ok = 1;
    for (i = 0; i < PORT_PSG_FRAME_SAMPLES; i++)
        ok &= lr[2 * i + 1] == RefOut(a[i] * 2, 0x4200) && lr[2 * i] == b[i] * 256;
    CHECK(ok, "FIFO A at 50%%");
    CHECK_EQ(RefOut(100 * 2, 0x4200), 100 * 128);
    CHECK_EQ(RefOut(-3 * 2, 0x4200), -512); /* 506 >> 2 = 126: -8 units */

    /* PSG alone: one channel at level 15, NR50 = 77h, 100%: 15 * 10h units,
     * 15360. NR51 routes it. */
    Boot();
    W8(REG_NR51, 0x44);
    ConstantWave();
    PortPsg_Render(NULL, NULL, lr, PORT_PSG_FRAME_SAMPLES);
    CHECK(AllFrames(lr, PORT_PSG_FRAME_SAMPLES, 15360, 15360), "ch3 both sides");
    W8(REG_NR51, 0x04);
    PortPsg_Latch();
    PortPsg_Render(NULL, NULL, lr, PORT_PSG_FRAME_SAMPLES);
    CHECK(AllFrames(lr, PORT_PSG_FRAME_SAMPLES, 0, 15360), "ch3 right only");

    /* NR50: (n + 1) / 8. Left 3 -> 120 units; right 0 -> 30 units, which
     * the 8-bit resolution cuts to 28 (542 >> 2 << 2 = 540). */
    W8(REG_NR51, 0x44);
    W8(REG_NR50, 0x30);
    PortPsg_Latch();
    PortPsg_Render(NULL, NULL, lr, PORT_PSG_FRAME_SAMPLES);
    CHECK(AllFrames(lr, PORT_PSG_FRAME_SAMPLES, 120 * 64, 28 * 64), "NR50 30h, 8 bits");
    /* At 9 bits (SOUNDBIAS 0200h) the 30 survives. */
    W16(REG_SOUNDBIAS, 0x0200);
    PortPsg_Latch();
    PortPsg_Render(NULL, NULL, lr, PORT_PSG_FRAME_SAMPLES);
    CHECK(AllFrames(lr, PORT_PSG_FRAME_SAMPLES, 120 * 64, 30 * 64), "NR50 30h, 9 bits");
    /* At 6 bits (C200h): 542 >> 4 << 4 = 528, 16 units; 632 -> 624, 112. */
    W16(REG_SOUNDBIAS, 0xC200);
    PortPsg_Latch();
    PortPsg_Render(NULL, NULL, lr, PORT_PSG_FRAME_SAMPLES);
    CHECK(AllFrames(lr, PORT_PSG_FRAME_SAMPLES, 112 * 64, 16 * 64), "NR50 30h, 6 bits");

    /* SOUNDCNT_H bits 0-1: 25%, 50%, 100%; 3 ("Prohibited") as 100%. */
    {
        static const int16_t want[4] = { 60 * 64, 120 * 64, 240 * 64, 240 * 64 };
        unsigned code;

        W16(REG_SOUNDBIAS, 0x4200);
        W8(REG_NR50, 0x77);
        for (code = 0; code < 4; code++)
        {
            W16(REG_SOUNDCNT_H, (uint16_t)(0xA90C | code));
            PortPsg_Latch();
            PortPsg_Render(NULL, NULL, lr, 16);
            CHECK(AllFrames(lr, 16, want[code], want[code]), "PSG volume code %u", code);
        }
    }

    /* Clipping. Right: 240 (PSG) + 127 * 4 (FIFO A) + 200h = 1260 > 3FFh:
     * 3FFh >> 2 << 2 = 3FCh, 508 units. */
    W16(REG_SOUNDCNT_H, 0xA90E);
    PortPsg_Latch();
    memset(a, 127, sizeof a);
    memset(b, 0, sizeof b);
    PortPsg_Render(a, b, lr, 16);
    CHECK(AllFrames(lr, 16, 15360, 508 * 64), "clip high");
    CHECK_EQ(RefOut(240 + 508, 0x4200), 508 * 64);
    /* Bias 0 (the port's SOUNDBIAS before the BIOS value is set): every
     * negative sample clips to 0. */
    W8(REG_NR51, 0x00);
    W16(REG_SOUNDBIAS, 0x4000);
    PortPsg_Latch();
    for (i = 0; i < 16; i++)
        a[i] = (int8_t)(i & 1 ? -64 : 64);
    PortPsg_Render(a, b, lr, 16);
    ok = 1;
    for (i = 0; i < 16; i++)
        ok &= lr[2 * i + 1] == (i & 1 ? 0 : 64 * 256);
    CHECK(ok, "bias 0 clips negative samples");

    /* The DAC rule over every 10-bit input, at all four resolutions and a
     * few bias levels. */
    {
        static const uint16_t biases[] = { 0x0200, 0x4200, 0x8200, 0xC200, 0x4000, 0x43FE, 0x4100 };
        unsigned k;
        int32_t mix;

        ok = 1;
        for (k = 0; k < sizeof biases / sizeof biases[0]; k++)
        {
            sPsg.soundbias = biases[k];
            for (mix = -0x700; mix <= 0x700; mix++)
            {
                int32_t got = DacTick(mix) - (biases[k] & 0x3FE);

                got = got * 64 > 32767 ? 32767 : (got * 64 < -32768 ? -32768 : got * 64);
                ok &= got == RefOut(mix, biases[k]);
            }
        }
        CHECK(ok, "DAC rule");
    }
}

/* The DAC ticks every 512 >> resolution cycles on the global clock: 4 or 5
 * per 1254-cycle sample at 65536 Hz, 4389 in 4 frames (4 * 280896 / 256). */
static void TestTicks(void)
{
    int16_t lr[2 * PORT_PSG_FRAME_SAMPLES];
    uint32_t t0;
    unsigned f;

    Boot();
    PortPsg_Latch();
    t0 = sPsg.pwmTicks;
    for (f = 0; f < 4; f++)
    {
        uint32_t c0 = sPsg.cycle, k0 = sPsg.pwmTicks;

        PortPsg_Render(NULL, NULL, lr, PORT_PSG_FRAME_SAMPLES);
        CHECK_EQ(sPsg.pwmTicks - k0, (c0 + PORT_PSG_FRAME_CYCLES) / 256u - c0 / 256u);
        CHECK_EQ(sPsg.cycle - c0, PORT_PSG_FRAME_CYCLES);
    }
    CHECK_EQ(sPsg.pwmTicks - t0, 4389);
    W16(REG_SOUNDBIAS, 0x0200);
    PortPsg_Latch();
    t0 = sPsg.pwmTicks;
    PortPsg_Render(NULL, NULL, lr, PORT_PSG_FRAME_SAMPLES);
    CHECK_EQ(sPsg.pwmTicks - t0, (4u * PORT_PSG_FRAME_CYCLES + PORT_PSG_FRAME_CYCLES) / 512u -
                                     4u * PORT_PSG_FRAME_CYCLES / 512u);
}

/* Render's box average, recomputed by hand: the DAC ticks at every multiple
 * of 256 cycles in (start, start + 1254] of each sample, the PSG side is
 * level * 8 * (NR50 + 1) >> (4 - 2) at 100%, and the sample is the mean of
 * (DAC - bias) * 64 over those ticks, rounded toward zero. Channel 2, 50%
 * duty, at a low pitch (4096 cycles a step) and a high one (64 cycles a
 * step, faster than the ticks). */
static void TestRenderAverage(void)
{
    static const uint16_t ns[] = { 1792, 2044, 2047 };
    static int16_t got[2 * PORT_PSG_FRAME_SAMPLES * 2];
    unsigned k, i;

    for (k = 0; k < sizeof ns / sizeof ns[0]; k++)
    {
        int ok = 1;
        uint32_t t = 0;

        for (i = 0; i < 2; i++)
        {
            Boot();
            W8(REG_NR51, 0x22);
            W8(REG_NR50, 0x75);
            W8(REG_NR21, 0x80);
            W8(REG_NR22, 0xF0);
            W8(REG_NR23, (uint8_t)ns[k]);
            W8(REG_NR24, (uint8_t)(0x80 | ns[k] >> 8));
            PortPsg_Latch();
            if (i == 0)
            {
                PortPsg_Render(NULL, NULL, got, 2 * PORT_PSG_FRAME_SAMPLES);
                continue;
            }
            for (unsigned s = 0; s < 2 * PORT_PSG_FRAME_SAMPLES; s++)
            {
                uint32_t end = (s + 1u) * 1254u;
                int32_t sumL = 0, sumR = 0, n = 0;

                while ((t / 256u + 1u) * 256u <= end)
                {
                    uint32_t tick = (t / 256u + 1u) * 256u;
                    int32_t level;

                    PortPsg_Run(tick - t);
                    t = tick;
                    level = PortPsg_ChannelLevel(2);
                    sumL += RefOut(level * 8 * (7 + 1) >> 2, 0x4200) / 64;
                    sumR += RefOut(level * 8 * (5 + 1) >> 2, 0x4200) / 64;
                    n++;
                }
                PortPsg_Run(end - t);
                t = end;
                if (got[2 * s] != (int16_t)(sumL * 64 / n) || got[2 * s + 1] != (int16_t)(sumR * 64 / n))
                {
                    if (ok)
                        printf("  n=%u sample %u: %d,%d want %d,%d\n", ns[k], s, got[2 * s], got[2 * s + 1],
                               sumL * 64 / n, sumR * 64 / n);
                    ok = 0;
                }
            }
        }
        CHECK(ok, "box average, n = %u", ns[k]);
    }
}

/* PortPsg_ResetIo: the BIOS boot values. */
static void TestResetIo(void)
{
    uint32_t i;
    int ok = 1;

    memset(gIo, 0xEE, sizeof gIo);
    PortPsg_ResetIo(gIo);
    for (i = 0; i < sizeof gIo; i++)
    {
        uint8_t want = 0xEE;

        if (i >= 0x60 && i < 0xA8)
            want = 0;
        if (i == REG_SOUNDBIAS + 1)
            want = 0x02;
        ok &= gIo[i] == want;
    }
    CHECK(ok, "4000060h-40000A7h zero, SOUNDBIAS 0200h, nothing else touched");
}

int main(void)
{
    TestDuty();
    TestSquareFrequency();
    TestEnvelope();
    TestSweep();
    TestLength();
    TestWave();
    TestLfsr();
    TestNoise();
    TestReadback();
    TestTrigger();
    TestMaster();
    TestMix();
    TestTicks();
    TestRenderAverage();
    TestResetIo();
    printf("test_psg: %d checks, %d failed\n", gChecks, gFails);
    return gFails != 0;
}
