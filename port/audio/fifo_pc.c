#include "global.h"
#include "gba/m4a_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port_hw.h"
#include "audio_pc.h"
#include "psg.h"

/*
 * The FIFO model (port-plan/audio-design.md 1.2-1.4; DIVERGENCES H-6, which
 * audio-design calls H-4).
 *
 * On the GBA, Timer 0 overflows every 1254 cycles (224 times a frame) and
 * each overflow makes FIFO A and FIFO B play one byte, refilled by DMA1 and
 * DMA2 from the driver's PCM ring (two lanes of 7 slots of 224 bytes).
 * m4aSoundVSync re-arms both DMAs when its counter reloads, so the read
 * restarts at the lane start every 7 VBlanks. Here, once per VBlank, after
 * the game's handler has run the driver:
 *
 *   c = SOUND_INFO_PTR->pcmDmaCounter (after m4aSoundVSync), p = period - c;
 *   for DMA1 and DMA2: enabled, FIFO start timing, destination FIFO A or B
 *     -> that FIFO plays bytes [SAD + p*224, +224) this frame;
 *   otherwise that FIFO plays nothing (silence).
 *
 * The counter is the DMA position because it reloads in the same VBlank that
 * re-arms the DMA (m4a_1.s:885-923). The bytes then go with the PSG to the
 * DAC model, PortPsg_Latch + PortPsg_Render (psg.c), which routes them by
 * SOUNDCNT_H, adds the PSG and the bias, clips and quantises as the hardware
 * does. Nothing else touches the samples.
 *
 * Environment (tests and tools; none is needed to play):
 *   FE8PC_AUDIO_DUMP=prefix  writes prefix.lr16 (the s16 L,R frames, 224 per
 *                            VBlank, little-endian) and prefix.fifo8 (per
 *                            VBlank, the 224 bytes FIFO A played, then FIFO
 *                            B's; zeros when silent). audio-design 1.5.
 *   FE8PC_SND_RECORD=path    one 8,236-byte record per VBlank, taken at line
 *                            160 before the VBlank IRQ, in the format of
 *                            port/test/oracle/sound_capture.lua (audio-design
 *                            5.2/5.3). FE8PC_SND_FROM=v skips records before v.
 *   FE8PC_AUDIO_LOG=1        logs each song start, by player and song number.
 */

#define FRAMES PORT_AUDIO_FRAMES_PER_VBLANK

#define IO_BASE    0x04000000u
#define FIFO_A     0x040000A0u
#define FIFO_B     0x040000A4u
#define DMA_ENABLE_BIT 0x8000u
#define DMA_TIMING_MASK 0x3000u
#define DMA_TIMING_SPECIAL 0x3000u /* FIFO request for DMA1/DMA2 */

static int16_t sLr[2 * FRAMES];
static uint32_t sVBlanks;      /* PortAudio_OnVBlank calls */
static uint32_t sPreVBlanks;   /* PortAudio_PreVBlank calls (record index v) */
static uint32_t sPlayed[2];    /* VBlanks in which FIFO A / B played a slot */

static FILE *sDumpLr, *sDumpFifo, *sRecord;
static uint32_t sRecordFrom;
static int sSongLog;

static uint32_t Rd32(uint32_t addr)
{
    return *(volatile uint32_t *)(uintptr_t)addr;
}

static uint16_t Rd16(uint32_t addr)
{
    return *(volatile uint16_t *)(uintptr_t)addr;
}

/* [addr, addr + n) lies in EWRAM or IWRAM (where the driver keeps its ring). */
static int InGbaRam(uint32_t addr, uint32_t n)
{
    return (addr >= 0x02000000u && addr <= 0x02040000u - n) ||
           (addr >= 0x03000000u && addr <= 0x03008000u - n);
}

static FILE *OpenOut(const char *prefix, const char *ext)
{
    char path[4096];
    FILE *f;

    if (snprintf(path, sizeof(path), "%s%s", prefix, ext) >= (int)sizeof(path))
        return NULL;
    f = fopen(path, "wb");
    if (f == NULL)
        PortLog("[audio] cannot write %s\n", path);
    else
        PortLog("[audio] writing %s\n", path);
    return f;
}

void PortAudio_Init(void)
{
    const char *s;

    PortPsg_Init((volatile uint8_t *)(uintptr_t)IO_BASE);

    if ((s = getenv("FE8PC_AUDIO_DUMP")) != NULL && s[0] != '\0')
    {
        sDumpLr = OpenOut(s, ".lr16");
        sDumpFifo = OpenOut(s, ".fifo8");
    }
    if ((s = getenv("FE8PC_SND_RECORD")) != NULL && s[0] != '\0')
    {
        sRecord = fopen(s, "wb");
        PortLog(sRecord != NULL ? "[audio] sound record to %s\n" : "[audio] cannot write %s\n", s);
        if ((s = getenv("FE8PC_SND_FROM")) != NULL)
            sRecordFrom = (uint32_t)strtoul(s, NULL, 0);
    }
    s = getenv("FE8PC_AUDIO_LOG");
    sSongLog = s != NULL && atoi(s) != 0;

    PortAudioOut_Init();
}

/*
 * Sound record (audio-design 5.2, 5.3): the same bytes the mGBA capture
 * reads, at the same point (line 160, before the CPU takes the VBlank IRQ).
 *   header   16 bytes: "FE8S", u16 version 1, u16 OnMain calls so far (mod
 *            65536; the GBA side counts logic ticks the same way), u32 v,
 *            u32 gGameClock
 *   IWRAM    0x03001E38-0x03002A67 (track arrays)               3,120
 *   IWRAM    0x03005410-0x0300679F (gSoundInfo ... gMPlayInfo_SE2) 5,008
 *   IWRAM    0x03007FF0 (SOUND_INFO_PTR)                            4
 *   I/O      0x04000060-0x040000AF (sound registers)               80
 *   I/O      DMA1CNT (0x040000C4), DMA2CNT (0x040000D0)             8
 * Code pointers in the driver structs are host addresses here; the compare
 * tool masks them.
 */
#define REC_SIZE (16 + 3120 + 5008 + 4 + 80 + 8)

static void WriteRecord(uint32_t v)
{
    static uint8_t buf[REC_SIZE];
    uint8_t *p = buf;
    uint32_t clock = Rd32(0x03000014u); /* gGameClock */
    uint32_t w;

    memcpy(p, "FE8S", 4);
    p[4] = 1, p[5] = 0;
    p[6] = (uint8_t)gPortOnMainCalls, p[7] = (uint8_t)(gPortOnMainCalls >> 8);
    memcpy(p + 8, &v, 4);
    memcpy(p + 12, &clock, 4);
    p += 16;
    memcpy(p, (const void *)(uintptr_t)0x03001E38u, 3120), p += 3120;
    memcpy(p, (const void *)(uintptr_t)0x03005410u, 5008), p += 5008;
    memcpy(p, (const void *)(uintptr_t)0x03007FF0u, 4), p += 4;
    memcpy(p, (const void *)(uintptr_t)0x04000060u, 80), p += 80;
    w = Rd32(0x040000C4u);
    memcpy(p, &w, 4), p += 4;
    w = Rd32(0x040000D0u);
    memcpy(p, &w, 4), p += 4;
    fwrite(buf, 1, sizeof(buf), sRecord);
}

void PortAudio_PreVBlank(void)
{
    if (sRecord != NULL && sPreVBlanks >= sRecordFrom)
        WriteRecord(sPreVBlanks);
    sPreVBlanks++;
}

uint32_t PortAudio_NextRecord(void)
{
    return sPreVBlanks;
}

/* FE8PC_AUDIO_LOG: a song starts when a player's header changes or its clock
   goes back (MPlayStart sets the header and clears the clock). */
static void LogSongStarts(void)
{
    static const struct SongHeader *sHeader[16];
    static u32 sClock[16];
    unsigned i, n = (unsigned)(uintptr_t)gNumMusicPlayers; /* ldscript.txt: 9 */

    for (i = 0; i < n; i++)
    {
        const struct MusicPlayerInfo *mp = gMPlayTable[i].info;
        const struct SongHeader *h = mp->songHeader;
        int song = -1;
        unsigned k;

        if (h != NULL && (h != sHeader[i] || mp->clock < sClock[i]))
        {
            for (k = 0; k < 1000; k++)
                if (gSongTable[k].header == h && gSongTable[k].ms == i)
                {
                    song = (int)k;
                    break;
                }
            PortLog("[audio] VBlank %u (frame %u): player %u starts song 0x%03X (header %p)\n",
                sVBlanks, gPortFrameCount, i, (unsigned)song, (const void *)h);
        }
        sHeader[i] = h;
        sClock[i] = mp->clock;
    }
}

void PortAudio_OnVBlank(void)
{
    static const int8_t sZeros[FRAMES];
    static int sWarnedSrc, sWarnedRate;
    const int8_t *lane[2] = { NULL, NULL };
    struct SoundInfo *si = SOUND_INFO_PTR;
    uint32_t p = 0;
    int dmaPos = 0;
    int d;

    /* The DMA position: slot period - c of each lane. */
    if (si != NULL && InGbaRam((uint32_t)(uintptr_t)si, sizeof(*si)))
    {
        uint32_t c = si->pcmDmaCounter, period = si->pcmDmaPeriod;

        if (c >= 1 && c <= period && period * FRAMES <= PCM_DMA_BUF_SIZE)
        {
            p = period - c;
            dmaPos = 1;
        }
        if (si->pcmSamplesPerVBlank != (s32)FRAMES && !sWarnedRate++)
            PortLog("[audio] pcmSamplesPerVBlank is %d, not 224: the FIFO model plays 224 per VBlank\n",
                (int)si->pcmSamplesPerVBlank);
    }
    for (d = 1; dmaPos && d <= 2; d++)
    {
        uint32_t regs = 0x040000B0u + 12u * (uint32_t)d; /* DMA1 0xBC, DMA2 0xC8 */
        uint32_t sad = Rd32(regs), dad = Rd32(regs + 4), src;
        uint16_t cnth = Rd16(regs + 10);
        int fifo = dad == FIFO_A ? 0 : dad == FIFO_B ? 1 : -1;

        if (!(cnth & DMA_ENABLE_BIT) || (cnth & DMA_TIMING_MASK) != DMA_TIMING_SPECIAL || fifo < 0)
            continue;
        src = sad + p * FRAMES;
        if (!InGbaRam(src, FRAMES))
        {
            if (!sWarnedSrc++)
                PortLog("[audio] DMA%d source 0x%08X is outside EWRAM/IWRAM: FIFO %c silent\n", d, src,
                    'A' + fifo);
            continue;
        }
        lane[fifo] = (const int8_t *)(uintptr_t)src;
    }
    sPlayed[0] += lane[0] != NULL;
    sPlayed[1] += lane[1] != NULL;

    /* The PSG reads the registers CgbSound wrote in this handler, then the DAC
       mixes all six sources. */
    PortPsg_Latch();
    PortPsg_Render(lane[0], lane[1], sLr, FRAMES);

    if (sDumpLr != NULL)
        fwrite(sLr, sizeof(sLr[0]), 2 * FRAMES, sDumpLr);
    if (sDumpFifo != NULL)
    {
        fwrite(lane[0] != NULL ? lane[0] : sZeros, 1, FRAMES, sDumpFifo);
        fwrite(lane[1] != NULL ? lane[1] : sZeros, 1, FRAMES, sDumpFifo);
    }
    if (sSongLog && si != NULL)
        LogSongStarts();

    PortAudioOut_Push(sLr, FRAMES);
    sVBlanks++;
}

void PortAudio_Report(void)
{
    PortLog("[audio] %u VBlanks, %u frames made (224 each); FIFO A played %u, FIFO B %u\n", sVBlanks,
        sVBlanks * FRAMES, sPlayed[0], sPlayed[1]);
    PortLog("[audio] images: TM0CNT_L 0x%04X TM0CNT_H 0x%04X SOUNDCNT_H 0x%04X SOUNDBIAS 0x%04X\n",
        Rd16(0x04000100u), Rd16(0x04000102u), Rd16(0x04000082u), Rd16(0x04000088u));
    PortAudioOut_Report();
    if (sDumpLr != NULL)
        fclose(sDumpLr);
    if (sDumpFifo != NULL)
        fclose(sDumpFifo);
    if (sRecord != NULL)
        fclose(sRecord);
    sDumpLr = sDumpFifo = sRecord = NULL;
}
