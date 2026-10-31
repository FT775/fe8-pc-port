/*
 * FE8 PC port: C version of src/m4a_1.s, the ARM/Thumb half of the m4a
 * (MusicPlayer2000) sound driver. Bit-exact with the ARM code.
 *
 * Derived from the C translation of pokeemerald-expansion's src/m4a_1.s in
 * pokeemerald-expansion-native (platform/m4a/m4a_mixer.c, m4a_render.c,
 * m4a_sequencer.c, m4a_commands.c, m4a_note.c, m4a_engine.h),
 * https://github.com/uehlbran/pokeemerald-expansion-native, commit
 * bb3fd057061cdd7e6a9e79b76abb41924ee44ecc (2026-10-15). That project's
 * notice follows; its licence covers only its own contribution, to the
 * extent it is separable from the decompiled code it translates.
 *
 * --------------------- LICENSE at that commit, verbatim ---------------------
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
 * Divergence ID: D-20 in port-plan/DIVERGENCES.md (a new port piece; ID given
 * in port-plan/DECISIONS.md, "IDs"). The kept departures below are listed
 * there too.
 *
 * Changes for FE8 (port-plan/audio-design.md section 2, and
 * community-reuse-audio-bios.md 2.1):
 * - FE8's m4a_1.s has no reversed, compressed or DPCM samples
 *   (SoundMainRAM_Unk1, SoundMainRAM_Unk2, sDecodingBuffer) and no sample
 *   start offset: a starting channel plays from wav->data with
 *   ct = wav->size, and ply_note does not copy MusicPlayerTrack.unk_3C.
 *   All of that is removed.
 * - FE8's older field names (status, ev, er, el, ct, cp, pp, np, func,
 *   intp, ...), and FE8's prototypes renamed where the asm's registers
 *   differ from include/gba/m4a_internal.h (design rule 2.2-2).
 * - The upstream departures for unreachable states are dropped: maxChans
 *   above 12, PSG channels 5-7, a data byte with no running status, a
 *   frame outside the ring and an odd frame size now do what the ARM code
 *   does. Under D1-a (ILP32, GBA memory at GBA addresses) the same
 *   addresses are touched. The sample mixers follow the ARM registers
 *   instruction by instruction, including the byte counter the ARM keeps
 *   in the top two bits of the output pointer. One catch: a data byte
 *   with no running status reads up to 128 bytes before gClockTable; that
 *   gives the GBA's bytes only while gClockTable is bound to its ROM copy
 *   (the default port build), not when src/m4a_tables.c is built for the
 *   host. mid2agb never writes such a byte.
 * - chk_adr_r2 also accepts a host image window (design rule 2.2-6).
 * - Kept departures (no other result is possible on a host): SoundMain and
 *   m4aSoundVSync return when SOUND_INFO_PTR is NULL (the GBA reads ident
 *   from the BIOS at 0, never ID_NUMBER); chk_adr_r2 checks before it
 *   reads (same result; a host read below 0x4000 faults).
 *
 * Needs ILP32 (D1-a): struct fields typed u32 hold pointers.
 * Tested against the ARM code in Unicorn: port/test/audio/.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The asm's register conventions differ from these header prototypes. */
#define MPlayMain    MPlayMain_header_prototype
#define ply_note     ply_note_header_prototype
#define SoundMainBTM SoundMainBTM_header_prototype
#include "gba/m4a_internal.h"
#undef MPlayMain
#undef ply_note
#undef SoundMainBTM

#include "m4a_1_pc.h"

_Static_assert(sizeof(void *) == 4, "m4a_1_pc.c needs ILP32 (D1-a)");
_Static_assert(sizeof(struct SoundChannel) == 0x40, "SoundChannel layout");
_Static_assert(sizeof(struct CgbChannel) == 0x40, "CgbChannel layout");
_Static_assert(sizeof(struct MusicPlayerTrack) == 0x50, "MusicPlayerTrack layout");
_Static_assert(sizeof(struct MusicPlayerInfo) == 0x40, "MusicPlayerInfo layout");
_Static_assert(offsetof(struct SoundInfo, chans) == 0x50, "SoundInfo.chans");
_Static_assert(offsetof(struct SoundInfo, pcmBuffer) == 0x350, "SoundInfo.pcmBuffer");
_Static_assert(offsetof(struct MusicPlayerTrack, cmdPtr) == 0x40, "MusicPlayerTrack.cmdPtr");
_Static_assert(offsetof(struct MusicPlayerInfo, func) == 0x38, "MusicPlayerInfo.func");

/* Defined in src/m4a.c and src/m4a_tables.c, used by the asm by name. */
extern const u8 gClockTable[];
extern void *const gMPlayJumpTableTemplate[];
u32 MidiKeyToFreq(struct WaveData *wav, u8 key, u8 fineAdjust);

typedef void (*M4aVoidFunc)(void);
typedef void (*M4aPlayerFunc)(struct MusicPlayerInfo *);
typedef void (*M4aCmdFunc)(struct MusicPlayerInfo *, struct MusicPlayerTrack *);
typedef void (*M4aNoteFunc)(u32, struct MusicPlayerInfo *, struct MusicPlayerTrack *);

/* GBATEK: VCOUNT reads 160..227 during VBlank; 228 lines per frame. */
#define VCOUNT_VBLANK   160u
#define TOTAL_SCANLINES 228u

/* Offsets the asm uses on chain nodes, which may be SoundChannel or
 * CgbChannel (they share these). */
#define CH_STATUS 0x00
#define CH_TYPE   0x01
#define CH_RVOL   0x02
#define CH_LVOL   0x03
#define CH_KEY    0x08
#define CH_GT     0x10
#define CH_MK     0x11
#define CH_VE     0x12
#define CH_PR     0x13
#define CH_RP     0x14
#define CH_CGB_MO 0x1D
#define CH_FREQ   0x20
#define CH_WAV    0x24
#define CH_TRACK  0x2C
#define CH_PP     0x30
#define CH_NP     0x34

/* Host image window that chk_adr_r2 also accepts (design rule 2.2-6). */
u32 gPortM4aHostImageLo = 0;
u32 gPortM4aHostImageHi = 0;

/* ------------------------------------------------------------------ */
/* Raw memory, as the ARM sees it (ILP32: an address is a u32).       */

static inline u8 *P8(u32 addr) { return (u8 *)(uintptr_t)addr; }
static inline u32 A(const void *p) { return (u32)(uintptr_t)p; }

static inline u32 Ld8(u32 addr) { return *(const u8 *)(uintptr_t)addr; }
static inline void St8(u32 addr, u32 v) { *(u8 *)(uintptr_t)addr = (u8)v; }

/* ldrsb: a byte sign-extended to 32 bits, as a u32 pattern. */
static inline u32 Sx8(u32 v) { v &= 0xFF; return v - ((v & 0x80u) << 1); }
static inline u32 Ldsb(u32 addr) { return Sx8(Ld8(addr)); }

/* ldr/str of a word. ARMv4: a store ignores the address's low bits; a load
 * reads the aligned word and rotates it right by 8 * (addr & 3). */
static inline u32 Ror(u32 v, u32 n) { n &= 31; return n ? (v >> n) | (v << (32 - n)) : v; }
static inline u32 Ld32(u32 addr)
{
    u32 w;

    memcpy(&w, (const void *)(uintptr_t)(addr & ~3u), 4);
    return Ror(w, (addr & 3u) * 8);
}
static inline void St32(u32 addr, u32 v) { memcpy((void *)(uintptr_t)(addr & ~3u), &v, 4); }

static inline u32 Ld32Field(u32 addr) { u32 w; memcpy(&w, (const void *)(uintptr_t)addr, 4); return w; }
static inline void St32Field(u32 addr, u32 v) { memcpy((void *)(uintptr_t)addr, &v, 4); }

/* asr by 1..31 of a u32 pattern. */
static inline u32 Asr(u32 v, u32 n) { return (v >> n) | ((v & 0x80000000u) ? ~(0xFFFFFFFFu >> n) : 0); }

/* Signed views for the flag tests (gt/le after subs/adds/cmp). */
static inline s32 S(u32 v) { return (s32)v; }

/* Thumb `subs rX, #k; bgt`: true when old > k as signed. */
static inline int GtAfterSub(u32 old, u32 k) { return (int64_t)S(old) - (int64_t)S(k) > 0; }

/* ------------------------------------------------------------------ */
/* umul3232H32                                                         */

u32 umul3232H32(u32 multiplier, u32 multiplicand)
{
    return (u32)(((uint64_t)multiplier * multiplicand) >> 32);
}

/* ------------------------------------------------------------------ */
/* chk_adr_r2, ldrb_r3_r2, ld_r3_tp_adr_i and its unchecked twin       */

/*
 * chk_adr_r2: may the driver keep a value it read at addr? Every address at
 * or above 0x02000000 passes. Below that, only BIOS addresses (< 0x4000) at
 * or after gMPlayJumpTableTemplate pass; FE8's template is in the cartridge
 * ROM, so on the GBA no address below 0x02000000 passes. The port also
 * accepts the window gPortM4aHostImageLo/Hi, which is empty: the Linux
 * (-Ttext-segment) and Windows (Makefile.mingw --image-base) builds both
 * link the host image at 0x10000000, which the first test already accepts.
 */
int chk_adr_r2(u32 addr)
{
    if ((addr >> 25) != 0)
        return 1;
    if (addr - gPortM4aHostImageLo < gPortM4aHostImageHi - gPortM4aHostImageLo)
        return 1;
    if (addr < A(gMPlayJumpTableTemplate))
        return 0;
    return (addr >> 14) == 0;
}

/* The asm loads first, then checks. Checking first gives the same value
 * without a host read below 0x4000. */
u32 ldrb_r3_r2(u32 addr)
{
    return chk_adr_r2(addr) ? Ld8(addr) : 0;
}

u32 ld_r3_tp_adr_i(struct MusicPlayerTrack *track)
{
    u32 p = A(track->cmdPtr);

    track->cmdPtr = P8(p + 1);
    return ldrb_r3_r2(p);
}

u32 ld_r3_tp_adr_i_unchecked(struct MusicPlayerTrack *track)
{
    u32 p = A(track->cmdPtr);

    track->cmdPtr = P8(p + 1);
    return Ld8(p);
}

/* ------------------------------------------------------------------ */
/* Small helpers from the asm                                          */

void SoundMainBTM(void *addr)
{
    /* four `stm r0!, {r1-r4}`: STM ignores the address's low two bits */
    memset((void *)((uintptr_t)addr & ~(uintptr_t)3), 0, 64);
}

void RealClearChain(void *x)
{
    u32 chan = A(x);
    u32 track = Ld32Field(chan + CH_TRACK);
    u32 np, pp;

    if (track == 0)
        return;
    np = Ld32Field(chan + CH_NP);
    pp = Ld32Field(chan + CH_PP);
    if (pp != 0)
        St32Field(pp + CH_NP, np);
    else
        St32Field(track + offsetof(struct MusicPlayerTrack, chan), np);
    if (np != 0)
        St32Field(np + CH_PP, pp);
    St32Field(chan + CH_TRACK, 0);
}

void clear_modM(struct MusicPlayerTrack *track)
{
    track->modM = 0;
    track->lfoSpeedC = 0;
    track->flags = (u8)(track->flags | (track->modT == 0 ? MPT_FLG_PITCHG : MPT_FLG_VOLCHG));
}

/* ChnVolSetAsm: the asm takes the channel in r4 and the track in r5. */
void ChnVolSetAsm(void *chanPtr, struct MusicPlayerTrack *track)
{
    u32 chan = A(chanPtr);
    u32 ve = Ld8(chan + CH_VE);
    u32 rp = Ldsb(chan + CH_RP);
    u32 v;

    v = Asr((0x80u + rp) * ve * track->volMR, 14);
    if (v > 0xFF)
        v = 0xFF;
    St8(chan + CH_RVOL, v);
    v = Asr((0x7Fu - rp) * ve * track->volML, 14);
    if (v > 0xFF)
        v = 0xFF;
    St8(chan + CH_LVOL, v);
}

/* ------------------------------------------------------------------ */
/* Jump table and commands                                             */

void MPlayJumpTableCopy(void **mplayJumpTable)
{
    u32 i;

    for (i = 0; i < 36; i++)
    {
        u32 entry = A(&gMPlayJumpTableTemplate[i]);

        mplayJumpTable[i] = chk_adr_r2(entry) ? (void *)(uintptr_t)Ld32Field(entry) : NULL;
    }
}

void ply_fine(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    u32 chan = A(track->chan);

    (void)mplayInfo;
    while (chan != 0)
    {
        u32 st = Ld8(chan + CH_STATUS);

        if (st & 0xC7)
            St8(chan + CH_STATUS, st | 0x40);
        RealClearChain(P8(chan));
        chan = Ld32Field(chan + CH_NP);
    }
    track->flags = 0;
}

/* ply_goto_1: bytes 3, 2, 1 unchecked, then byte 0 through ldrb_r3_r2. */
static void GotoAt(struct MusicPlayerTrack *track)
{
    u32 p = A(track->cmdPtr);
    u32 v = (Ld8(p + 3) << 24) | (Ld8(p + 2) << 16) | (Ld8(p + 1) << 8);

    track->cmdPtr = P8(v | ldrb_r3_r2(p));
}

void ply_goto(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    GotoAt(track);
}

void ply_patt(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    u32 level = track->patternLevel;

    if (level >= 3)
    {
        ply_fine(mplayInfo, track);
        return;
    }
    track->patternStack[level] = P8(A(track->cmdPtr) + 4);
    track->patternLevel = (u8)(level + 1);
    GotoAt(track);
}

void ply_pend(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    u32 level = track->patternLevel;

    (void)mplayInfo;
    if (level == 0)
        return;
    level--;
    track->patternLevel = (u8)level;
    track->cmdPtr = track->patternStack[level];
}

void ply_rept(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    u32 args = A(track->cmdPtr);
    u32 rep;

    (void)mplayInfo;
    if (Ld8(args) == 0)
    {
        track->cmdPtr = P8(args + 1);
        GotoAt(track);
        return;
    }
    rep = (u32)track->repN + 1; /* compared before it wraps */
    track->repN = (u8)rep;
    if (rep < ld_r3_tp_adr_i(track))
    {
        GotoAt(track);
        return;
    }
    track->repN = 0;
    track->cmdPtr = P8(args + 5);
}

void ply_prio(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    track->priority = (u8)ld_r3_tp_adr_i(track);
}

void ply_tempo(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    u32 t = ld_r3_tp_adr_i(track) << 1;

    mplayInfo->tempoD = (u16)t;
    mplayInfo->tempoI = (u16)((t * mplayInfo->tempoU) >> 8);
}

void ply_keysh(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    track->keyShift = (s8)ld_r3_tp_adr_i(track);
    track->flags |= 0xC;
}

/* ply_voice: the index byte is unchecked; the three words of the voice are
 * all checked against the voice's own address (r2). */
void ply_voice(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    u32 idx = ld_r3_tp_adr_i_unchecked(track);
    u32 tone = A(mplayInfo->tone) + idx * 12;
    u32 dst = A(&track->tone);
    int ok = chk_adr_r2(tone);

    St32Field(dst + 0, ok ? Ld32Field(tone + 0) : 0);
    St32Field(dst + 4, ok ? Ld32Field(tone + 4) : 0);
    St32Field(dst + 8, ok ? Ld32Field(tone + 8) : 0);
}

void ply_vol(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    track->vol = (u8)ld_r3_tp_adr_i(track);
    track->flags |= 0x3;
}

void ply_pan(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    track->pan = (s8)(ld_r3_tp_adr_i(track) - 0x40);
    track->flags |= 0x3;
}

void ply_bend(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    track->bend = (s8)(ld_r3_tp_adr_i(track) - 0x40);
    track->flags |= 0xC;
}

void ply_bendr(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    track->bendRange = (u8)ld_r3_tp_adr_i(track);
    track->flags |= 0xC;
}

void ply_lfodl(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    track->lfoDelay = (u8)ld_r3_tp_adr_i(track);
}

void ply_modt(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    u32 v = ld_r3_tp_adr_i(track);

    (void)mplayInfo;
    if (track->modT == v)
        return;
    track->modT = (u8)v;
    track->flags |= 0xF;
}

void ply_tune(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    track->tune = (s8)(ld_r3_tp_adr_i(track) - 0x40);
    track->flags |= 0xC;
}

/* ply_port: writes the (checked) second byte to REG_SOUND1CNT_L + the
 * (unchecked) first byte. No FE8 song uses it; the write lands in the I/O
 * image. */
void ply_port(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    u32 p = A(track->cmdPtr);
    u32 reg = REG_ADDR_SOUND1CNT_L + Ld8(p);

    (void)mplayInfo;
    track->cmdPtr = P8(p + 2);
    *(vu8 *)(uintptr_t)reg = (u8)ldrb_r3_r2(p + 1);
}

void ply_lfos(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    u32 v = ld_r3_tp_adr_i_unchecked(track);

    (void)mplayInfo;
    track->lfoSpeed = (u8)v;
    if (v == 0)
        clear_modM(track);
}

void ply_mod(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    u32 v = ld_r3_tp_adr_i_unchecked(track);

    (void)mplayInfo;
    track->mod = (u8)v;
    if (v == 0)
        clear_modM(track);
}

void ply_endtie(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    u32 p = A(track->cmdPtr);
    u32 key = Ld8(p);
    u32 chan;

    (void)mplayInfo;
    if (key < 0x80)
    {
        track->key = (u8)key;
        track->cmdPtr = P8(p + 1);
    }
    else
    {
        key = track->key;
    }

    for (chan = A(track->chan); chan != 0; chan = Ld32Field(chan + CH_NP))
    {
        u32 st = Ld8(chan + CH_STATUS);

        if ((st & 0x83) && !(st & 0x40) && Ld8(chan + CH_MK) == key)
        {
            St8(chan + CH_STATUS, st | 0x40);
            return;
        }
    }
}

/* ------------------------------------------------------------------ */
/* TrackStop                                                           */

void TrackStop(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    u32 chan;

    (void)mplayInfo;
    if (!(track->flags & 0x80))
        return;
    for (chan = A(track->chan); chan != 0; chan = Ld32Field(chan + CH_NP))
    {
        if (Ld8(chan + CH_STATUS) != 0)
        {
            u32 cgb = Ld8(chan + CH_TYPE) & 7;

            if (cgb != 0)
                SOUND_INFO_PTR->CgbOscOff((u8)cgb);
            St8(chan + CH_STATUS, 0);
        }
        St32Field(chan + CH_TRACK, 0);
    }
    track->chan = NULL;
}

/* ------------------------------------------------------------------ */
/* ply_note                                                            */

void ply_note(u32 lenIdx, struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    struct SoundInfo *soundInfo = SOUND_INFO_PTR;
    u32 t = A(track);
    u32 p, b;
    u32 tone;       /* r9: the voice played */
    u32 key;        /* sp+8 */
    u32 rhythmPan = 0; /* sp+0x14 */
    u32 priority;   /* sp+0x10 */
    u32 cgb;        /* sp+0xC */
    u32 chan;       /* r4 */
    u32 wav;        /* r7 */
    s32 sk;

    track->gateTime = *(const u8 *)(uintptr_t)(A(gClockTable) + lenIdx);

    p = A(track->cmdPtr);
    b = Ld8(p);
    if (b < 0x80)
    {
        track->key = (u8)b;
        p++;
        b = Ld8(p);
        if (b < 0x80)
        {
            track->velocity = (u8)b;
            p++;
            b = Ld8(p);
            if (b < 0x80)
            {
                track->gateTime = (u8)(track->gateTime + b);
                p++;
            }
        }
        track->cmdPtr = P8(p);
    }

    {
        u32 type = track->tone.type;

        if (type & (TONEDATA_TYPE_RHY | TONEDATA_TYPE_SPL))
        {
            u32 idx;

            key = track->key;
            if (type & TONEDATA_TYPE_SPL)
                idx = Ld8(Ld32Field(t + offsetof(struct MusicPlayerTrack, tone) + 8) + key);
            else
                idx = key;
            tone = A(track->tone.wav) + idx * 12;
            if (Ld8(tone) & 0xC0)
                return;
            if (type & TONEDATA_TYPE_RHY)
            {
                u32 ps = Ld8(tone + 3);

                if (ps & 0x80)
                    rhythmPan = (ps - 0xC0) << 1;
                key = Ld8(tone + 1);
            }
        }
        else
        {
            tone = A(&track->tone);
            key = track->key;
        }
    }

    priority = (u32)mplayInfo->priority + track->priority;
    if (priority > 0xFF)
        priority = 0xFF;

    cgb = Ld8(tone) & 7;
    if (cgb != 0)
    {
        u32 st;

        chan = A(soundInfo->cgbChans);
        if (chan == 0)
            return;
        chan += (cgb - 1) << 6;
        st = Ld8(chan + CH_STATUS);
        if ((st & 0xC7) && !(st & 0x40))
        {
            u32 pr = Ld8(chan + CH_PR);

            if (pr > priority)
                return;
            if (pr == priority && Ld32Field(chan + CH_TRACK) < t)
                return;
        }
    }
    else
    {
        u32 bestPr = priority; /* r6 */
        u32 bestTrack = t;     /* r7 */
        u32 found = 0;         /* r2: a released channel was seen */
        u32 n;
        u32 c;

        chan = 0; /* r8 */
        n = soundInfo->maxChans;
        c = A(soundInfo) + 0x50;
        for (;;)
        {
            u32 st = Ld8(c + CH_STATUS);
            int take = 0;

            if (!(st & 0xC7))
            {
                chan = c;
                goto link;
            }
            if (st & 0x40)
            {
                if (found == 0)
                {
                    found = 1;
                    bestPr = Ld8(c + CH_PR);
                    bestTrack = Ld32Field(c + CH_TRACK);
                    take = 1;
                }
                else
                {
                    goto compare;
                }
            }
            else if (found == 0)
            {
            compare:
                {
                    u32 pr = Ld8(c + CH_PR);

                    if (pr < bestPr)
                    {
                        bestPr = pr;
                        bestTrack = Ld32Field(c + CH_TRACK);
                        take = 1;
                    }
                    else if (pr == bestPr)
                    {
                        u32 tr = Ld32Field(c + CH_TRACK);

                        if (tr > bestTrack)
                        {
                            bestTrack = tr;
                            take = 1;
                        }
                        else if (tr == bestTrack)
                        {
                            take = 1;
                        }
                    }
                }
            }
            if (take)
                chan = c;
            c += 0x40;
            if (!GtAfterSub(n, 1))
                break;
            n--;
        }
        if (chan == 0)
            return;
    }

link:
    ClearChain(P8(chan));
    St32Field(chan + CH_PP, 0);
    {
        u32 head = A(track->chan);

        St32Field(chan + CH_NP, head);
        if (head != 0)
            St32Field(head + CH_PP, chan);
    }
    track->chan = (struct SoundChannel *)P8(chan);
    St32Field(chan + CH_TRACK, t);

    track->lfoDelayC = track->lfoDelay;
    if (track->lfoDelay != 0)
        clear_modM(track);

    TrkVolPitSet(mplayInfo, track);

    /* gateTime, key, velocity, runningStatus as one word into gt, mk, ve, pr */
    St32Field(chan + CH_GT, Ld32Field(t + 4));
    St8(chan + CH_PR, priority);
    St8(chan + CH_KEY, key);
    St8(chan + CH_RP, rhythmPan);
    St8(chan + CH_TYPE, Ld8(tone));
    wav = Ld32Field(tone + 4);
    St32Field(chan + CH_WAV, wav);
    St32Field(chan + 4, Ld32Field(tone + 8)); /* attack, decay, sustain, release */
    {
        u16 echo;

        memcpy(&echo, (const u8 *)track + 0x1E, 2); /* echoVolume, echoLength */
        memcpy(P8(chan + 0xC), &echo, 2);
    }

    ChnVolSetAsm(P8(chan), track);

    sk = (s32)(Ld8(chan + CH_KEY) + Ldsb(t + 8)); /* + keyM */
    if (sk < 0)
        sk = 0;

    if (cgb != 0)
    {
        u32 ps;

        St8(chan + 0x1E, Ld8(tone + 2)); /* le = length */
        ps = Ld8(tone + 3);
        if ((ps & 0x80) || !(ps & 0x70))
            ps = 8;
        St8(chan + 0x1F, ps); /* sw = sweep */
        St32Field(chan + CH_FREQ, soundInfo->MidiKeyToCgbFreq((u8)cgb, (u8)sk, track->pitM));
    }
    else
    {
        St32Field(chan + CH_FREQ, MidiKeyToFreq((struct WaveData *)P8(wav), (u8)sk, track->pitM));
    }
    St8(chan + CH_STATUS, 0x80);
    track->flags &= 0xF0;
}

/* ------------------------------------------------------------------ */
/* MPlayMain                                                           */

/* _081DD87C: one tick of one existing track. Returns 0 when a jump-table
 * command left the track's flags at 0 (the asm skips to the next track). */
static void TickTrack(struct SoundInfo *soundInfo, struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    u32 chan;

    for (chan = A(track->chan); chan != 0; chan = Ld32Field(chan + CH_NP))
    {
        u32 st = Ld8(chan + CH_STATUS);

        if (st & 0xC7)
        {
            u32 g = Ld8(chan + CH_GT);

            if (g != 0)
            {
                g--;
                St8(chan + CH_GT, g);
                if (g == 0)
                    St8(chan + CH_STATUS, st | 0x40);
            }
        }
        else
        {
            ClearChain(P8(chan));
        }
    }

    if (track->flags & MPT_FLG_START)
    {
        Clear64byte(track);
        track->flags = MPT_FLG_EXIST;
        track->bendRange = 2;
        track->volX = 0x40;
        track->lfoSpeed = 0x16;
        track->tone.type = 1;
    }

    while (track->wait == 0)
    {
        u32 p = A(track->cmdPtr);
        u32 cmd = Ld8(p);

        if (cmd < 0x80)
        {
            cmd = track->runningStatus;
        }
        else
        {
            track->cmdPtr = P8(p + 1);
            if (cmd >= 0xBD)
                track->runningStatus = (u8)cmd;
        }

        if (cmd >= 0xCF)
        {
            ((M4aNoteFunc)(uintptr_t)soundInfo->plynote)(cmd - 0xCF, mplayInfo, track);
        }
        else if (cmd > 0xB0)
        {
            u32 idx = cmd - 0xB1;

            mplayInfo->cmd = (u8)idx;
            ((M4aCmdFunc)(uintptr_t)Ld32Field(soundInfo->MPlayJumpTable + idx * 4))(mplayInfo, track);
            if (track->flags == 0)
                return;
        }
        else
        {
            /* a data byte with no running status reads before the table, as on the GBA */
            track->wait = Ld8(A(gClockTable) + (cmd - 0x80));
        }
    }

    track->wait--;

    {
        u32 spd = track->lfoSpeed;
        u32 c, w, out;

        if (spd == 0 || track->mod == 0)
            return;
        if (track->lfoDelayC != 0)
        {
            track->lfoDelayC--;
            return;
        }
        c = (u32)track->lfoSpeedC + spd; /* the wave uses the unwrapped sum */
        track->lfoSpeedC = (u8)c;
        if (((c - 0x40) << 24) & 0x80000000u)
            w = Sx8(c);
        else
            w = 0x80u - c;
        out = Asr((u32)track->mod * w, 6);
        if ((((u32)(u8)track->modM ^ out) << 24) == 0)
            return;
        track->modM = (s8)out;
        track->flags = (u8)(track->flags | (track->modT != 0 ? 0x3 : 0xC));
    }
}

void MPlayMain(struct MusicPlayerInfo *mplayInfo)
{
    struct SoundInfo *soundInfo;
    u32 tempo;

    if (mplayInfo->ident != ID_NUMBER)
        return;
    mplayInfo->ident = ID_NUMBER + 1;

    if (mplayInfo->func != 0)
        ((M4aPlayerFunc)(uintptr_t)mplayInfo->func)((struct MusicPlayerInfo *)(uintptr_t)mplayInfo->intp);

    if (S(mplayInfo->status) < 0)
        goto done;
    soundInfo = SOUND_INFO_PTR;
    FadeOutBody(mplayInfo);
    if (S(mplayInfo->status) < 0)
        goto done;

    tempo = (u32)mplayInfo->tempoC + mplayInfo->tempoI;
    for (;;)
    {
        u32 n, bit, playing;
        struct MusicPlayerTrack *track;

        mplayInfo->tempoC = (u16)tempo;
        if (tempo < 150)
            break;

        n = mplayInfo->trackCount;
        track = mplayInfo->tracks;
        bit = 1;
        playing = 0;
        for (;;)
        {
            if (track->flags & MPT_FLG_EXIST)
            {
                playing |= bit;
                TickTrack(soundInfo, mplayInfo, track);
            }
            if (!GtAfterSub(n, 1))
                break;
            n--;
            track = (struct MusicPlayerTrack *)P8(A(track) + 0x50);
            bit <<= 1;
        }

        mplayInfo->clock++;
        if (playing == 0)
        {
            mplayInfo->status = MUSICPLAYER_STATUS_PAUSE;
            goto done;
        }
        mplayInfo->status = playing;
        tempo = (u32)mplayInfo->tempoC - 150;
    }

    {
        u32 n = mplayInfo->trackCount;
        u32 t = A(mplayInfo->tracks);

        for (;;)
        {
            struct MusicPlayerTrack *track = (struct MusicPlayerTrack *)P8(t);
            u32 f = track->flags;

            if ((f & 0x80) && (f & 0xF))
            {
                u32 chan;

                TrkVolPitSet(mplayInfo, track);
                for (chan = A(track->chan); chan != 0; chan = Ld32Field(chan + CH_NP))
                {
                    u32 cgb;

                    if (!(Ld8(chan + CH_STATUS) & 0xC7))
                    {
                        ClearChain(P8(chan));
                        continue;
                    }
                    cgb = Ld8(chan + CH_TYPE) & 7;
                    if (track->flags & 0x3)
                    {
                        ChnVolSetAsm(P8(chan), track);
                        if (cgb != 0)
                            St8(chan + CH_CGB_MO, Ld8(chan + CH_CGB_MO) | 1);
                    }
                    if (track->flags & 0xC)
                    {
                        s32 key = (s32)(Ld8(chan + CH_KEY) + Ldsb(t + 8)); /* + keyM */

                        if (key < 0)
                            key = 0;
                        if (cgb != 0)
                        {
                            St32Field(chan + CH_FREQ, soundInfo->MidiKeyToCgbFreq((u8)cgb, (u8)key, track->pitM));
                            St8(chan + CH_CGB_MO, Ld8(chan + CH_CGB_MO) | 2);
                        }
                        else
                        {
                            St32Field(chan + CH_FREQ,
                                MidiKeyToFreq((struct WaveData *)P8(Ld32Field(chan + CH_WAV)), (u8)key, track->pitM));
                        }
                    }
                }
                track->flags &= 0xF0;
            }
            if (!GtAfterSub(n, 1))
                break;
            n--;
            /* adds r5, #0x50; bgt: true for every address below 0x7FFFFFB0 */
            if ((int64_t)S(t) + 0x50 <= 0)
                break;
            t += 0x50;
        }
    }

done:
    mplayInfo->ident = ID_NUMBER;
}

/* ------------------------------------------------------------------ */
/* m4aSoundVSync                                                       */

void m4aSoundVSync(void)
{
    struct SoundInfo *soundInfo = SOUND_INFO_PTR;
    u32 c;

    if (soundInfo == NULL)
        return;
    if (soundInfo->ident - ID_NUMBER > 1)
        return;

    c = soundInfo->pcmDmaCounter;
    soundInfo->pcmDmaCounter = (u8)(c - 1);
    if (GtAfterSub(c, 1))
        return;
    soundInfo->pcmDmaCounter = soundInfo->pcmDmaPeriod;

    if (*(vu32 *)REG_ADDR_DMA1CNT & ((u32)DMA_REPEAT << 16))
        *(vu32 *)REG_ADDR_DMA1CNT = (((u32)(DMA_ENABLE | DMA_START_NOW | DMA_32BIT | DMA_SRC_INC | DMA_DEST_FIXED)) << 16) | 4;
    if (*(vu32 *)REG_ADDR_DMA2CNT & ((u32)DMA_REPEAT << 16))
        *(vu32 *)REG_ADDR_DMA2CNT = (((u32)(DMA_ENABLE | DMA_START_NOW | DMA_32BIT | DMA_SRC_INC | DMA_DEST_FIXED)) << 16) | 4;
    *(vu16 *)REG_ADDR_DMA1CNT_H = DMA_32BIT;
    *(vu16 *)REG_ADDR_DMA2CNT_H = DMA_32BIT;
    *(vu16 *)REG_ADDR_DMA1CNT_H = DMA_ENABLE | DMA_START_SPECIAL | DMA_32BIT | DMA_REPEAT;
    *(vu16 *)REG_ADDR_DMA2CNT_H = DMA_ENABLE | DMA_START_SPECIAL | DMA_32BIT | DMA_REPEAT;
}

/* ------------------------------------------------------------------ */
/* The mixer: SoundMainRAM                                             */

/* The difftest (port/test/audio) builds a second copy with this hook, so
 * VCOUNT can move during a call as it does on the GBA. */
#ifdef PORT_M4A_TEST_VCOUNT
u32 PortM4aTestVcount(void);
#define READ_VCOUNT() PortM4aTestVcount()
#else
#define READ_VCOUNT() (*(vu8 *)REG_ADDR_VCOUNT)
#endif

/* VCOUNT, made monotonic across VBlank (the maxLines check). */
static u32 CurrentLine(void)
{
    u32 line = READ_VCOUNT();

    if (line < VCOUNT_VBLANK)
        line += TOTAL_SCANLINES;
    return line;
}

/* The per-channel state the ARM keeps in registers and on its stack. */
struct MixRegs
{
    u32 r2;       /* samples left in the wave (ct) */
    u32 r3;       /* sample pointer (cp) */
    u32 r5;       /* output pointer; bits 30-31 count the bytes of a word */
    u32 r8;       /* output samples left */
    u32 r10, r11; /* er << 16, el << 16 */
    u32 loopStart; /* sp+0xC */
    u32 loopLen;   /* sp+0x10 */
};

/* mul r1, r10, r0; bic r1, r1, #0xFF0000; add r6, r1, r6, ror #8 */
static inline u32 MixByte(u32 word, u32 vol16, u32 sample)
{
    return ((vol16 * sample) & ~0xFF0000u) + Ror(word, 8);
}

/* adds r5, r5, #0x40000000; bcc: next byte of the word, carry after the 4th */
static inline int NextByte(u32 *r5)
{
    int carry = *r5 >= 0xC0000000u;

    *r5 += 0x40000000u;
    return carry;
}

/* _081DD174: the wave ended in the middle of a word. Rotates the two words
 * back into place, stores them, and turns the channel off. */
static void EndChannel(u32 chan, u32 r5, u32 r6, u32 r7)
{
    u32 rot = (3u - (r5 >> 30)) << 3;

    St8(chan + CH_STATUS, 0);
    r5 &= ~0xC0000000u;
    St32(r5 + PCM_DMA_BUF_SIZE, Ror(r7, rot));
    St32(r5, Ror(r6, rot));
}

/* _081DD07C: TONEDATA_TYPE_FIX, one stored sample per output sample. */
static void MixFixed(u32 chan, struct MixRegs *m)
{
    u32 r2 = m->r2, r3 = m->r3, r5 = m->r5, r8 = m->r8;
    u32 r6, r7, lr, s, old;

L07C:
    if (S(r2) <= 4)
        goto L0EC;
    old = r2;
    r2 = r2 - r8;
    if (S(old) > S(r8))
    {
        lr = 0;
    }
    else
    {
        lr = r8;
        r2 = r2 + r8;
        r8 = r2 - 4;
        lr = lr - r8;
        r2 &= 3;
        if (r2 == 0)
            r2 = 4;
    }
L0A8:
    r6 = Ld32(r5);
    r7 = Ld32(r5 + PCM_DMA_BUF_SIZE);
    do
    {
        s = Ldsb(r3);
        r3++;
        r6 = MixByte(r6, m->r10, s);
        r7 = MixByte(r7, m->r11, s);
    } while (!NextByte(&r5));
    St32(r5 + PCM_DMA_BUF_SIZE, r7);
    St32(r5, r6);
    r5 += 4;
    old = r8;
    r8 -= 4;
    if (GtAfterSub(old, 4))
        goto L0A8;
    r8 += lr;
    if (r8 == 0)
        goto L22C;
L0EC:
    r6 = Ld32(r5);
    r7 = Ld32(r5 + PCM_DMA_BUF_SIZE);
L0F4:
    s = Ldsb(r3);
    r3++;
    r6 = MixByte(r6, m->r10, s);
    r7 = MixByte(r7, m->r11, s);
    r2 -= 1;
    if (r2 == 0)
        goto L164;
L118:
    if (!NextByte(&r5))
        goto L0F4;
    St32(r5 + PCM_DMA_BUF_SIZE, r7);
    St32(r5, r6);
    r5 += 4;
    old = r8;
    r8 -= 4;
    if (GtAfterSub(old, 4))
        goto L07C;
    goto L22C;
L164:
    r2 = m->loopLen;
    if (r2 != 0)
    {
        r3 = m->loopStart;
        goto L118;
    }
    EndChannel(chan, r5, r6, r7);
    return;
L22C:
    St32Field(chan + 0x18, r2); /* ct */
    St32Field(chan + 0x28, r3); /* cp */
}

/* _081DD19C: interpolated, step divFreq * freq in 9.23 fixed point. */
static void MixInterp(u32 chan, struct MixRegs *m, u32 divFreq)
{
    u32 r2 = m->r2, r3 = m->r3, r5 = m->r5, r8 = m->r8;
    u32 lr = Ld32Field(chan + 0x1C); /* fw */
    u32 step = divFreq * Ld32Field(chan + CH_FREQ);
    u32 r0, r1, r6, r7, r9, old;

    r0 = Ldsb(r3);
    r3 += 1;
    r1 = Ldsb(r3) - r0;
L1B4:
    r6 = Ld32(r5);
    r7 = Ld32(r5 + PCM_DMA_BUF_SIZE);
L1BC:
    r9 = r0 + Asr(lr * r1, 23);
    r6 = MixByte(r6, m->r10, r9);
    r7 = MixByte(r7, m->r11, r9);
    lr += step;
    r9 = lr >> 23;
    if (r9 == 0)
        goto L208;
    lr &= ~0x3F800000u;
    old = r2;
    r2 -= r9;
    if (S(old) <= S(r9))
        goto L134;
    r9 -= 1;
    if (r9 == 0)
    {
        r0 = r0 + r1;
    }
    else
    {
        r3 += r9;
        r0 = Ldsb(r3);
    }
L1FC_tail:
    r3 += 1;
    r1 = Ldsb(r3) - r0;
L208:
    if (!NextByte(&r5))
        goto L1BC;
    St32(r5 + PCM_DMA_BUF_SIZE, r7);
    St32(r5, r6);
    r5 += 4;
    old = r8;
    r8 -= 4;
    if (GtAfterSub(old, 4))
        goto L1B4;
    r3 -= 1;
    St32Field(chan + 0x1C, lr); /* fw */
    St32Field(chan + 0x18, r2); /* ct */
    St32Field(chan + 0x28, r3); /* cp */
    return;
L134:
    if (m->loopLen == 0)
    {
        EndChannel(chan, r5, r6, r7);
        return;
    }
    r3 = m->loopStart;
    r9 = 0u - r2;
    for (;;)
    {
        int gt = (int64_t)S(m->loopLen) + (int64_t)S(r2) > 0;

        r2 = m->loopLen + r2;
        if (gt)
            break;
        r9 -= m->loopLen;
    }
    r3 += r9;
    r0 = Ldsb(r3);
    goto L1FC_tail;
}

/* SoundMainRAM_ChanLoop body for one channel with (status & 0xC7) != 0. */
static void MixChannel(struct SoundInfo *soundInfo, u32 chan, u32 wav, u32 r6, u32 frame, u32 samples, u32 divFreq)
{
    u32 r5;
    struct MixRegs m;

    if (r6 & 0x80)
    {
        if (r6 & 0x40)
        {
            St8(chan + CH_STATUS, 0);
            return;
        }
        r6 = 3;
        St8(chan + CH_STATUS, r6);
        St32Field(chan + 0x28, wav + 0x10);           /* cp = wav->data */
        St32Field(chan + 0x18, Ld32Field(wav + 0xC)); /* ct = wav->size */
        r5 = 0;
        St8(chan + 0x9, 0);       /* ev */
        St32Field(chan + 0x1C, 0); /* fw */
        if (Ld8(wav + 3) & 0xC0)
        {
            r6 |= 0x10;
            St8(chan + CH_STATUS, r6);
        }
        goto attack;
    }

    r5 = Ld8(chan + 0x9); /* ev */
    if (r6 & 0x4)
    {
        u32 iel = Ld8(chan + 0xD); /* echoLength */

        St8(chan + 0xD, iel - 1);
        if (iel > 1)
            goto store;
        St8(chan + CH_STATUS, 0);
        return;
    }
    if (r6 & 0x40)
    {
        r5 = (r5 * Ld8(chan + 0x7)) >> 8; /* release */
        if (r5 > Ld8(chan + 0xC))         /* echoVolume */
            goto store;
    echo:
        r5 = Ld8(chan + 0xC);
        if (r5 == 0)
        {
            St8(chan + CH_STATUS, 0);
            return;
        }
        r6 |= 0x4;
        St8(chan + CH_STATUS, r6);
        goto store;
    }
    switch (r6 & 3)
    {
    case 2:
    {
        u32 sus;

        r5 = (r5 * Ld8(chan + 0x5)) >> 8; /* decay */
        sus = Ld8(chan + 0x6);            /* sustain */
        if (r5 > sus)
            goto store;
        r5 = sus;
        if (r5 == 0)
            goto echo;
        r6 -= 1;
        St8(chan + CH_STATUS, r6);
        goto store;
    }
    case 3:
        goto attack;
    default:
        goto store;
    }

attack:
    r5 += Ld8(chan + 0x4); /* attack */
    if (r5 >= 0xFF)
    {
        r5 = 0xFF;
        r6 -= 1;
        St8(chan + CH_STATUS, r6);
    }

store:
    St8(chan + 0x9, r5); /* ev */
    r5 = ((soundInfo->masterVolume + 1u) * r5) >> 4;
    St8(chan + 0xA, (Ld8(chan + CH_RVOL) * r5) >> 8); /* er */
    St8(chan + 0xB, (Ld8(chan + CH_LVOL) * r5) >> 8); /* el */

    m.loopLen = r6 & 0x10;
    m.loopStart = 0;
    if (m.loopLen != 0)
    {
        u32 ls = Ld32Field(wav + 0x8);

        m.loopStart = wav + 0x10 + ls;
        m.loopLen = Ld32Field(wav + 0xC) - ls;
    }
    m.r5 = frame;
    m.r2 = Ld32Field(chan + 0x18); /* ct */
    m.r3 = Ld32Field(chan + 0x28); /* cp */
    m.r8 = samples;
    m.r10 = Ld8(chan + 0xA) << 16;
    m.r11 = Ld8(chan + 0xB) << 16;

    if (Ld8(chan + CH_TYPE) & TONEDATA_TYPE_FIX)
        MixFixed(chan, &m);
    else
        MixInterp(chan, &m, divFreq);
}

/* SoundMainRAM: the reverb or the clear of this frame, then the channels.
 * r4 = pcmDmaCounter, r5 = the frame, r8 = samples per frame. */
static void SoundMainRAM_C(struct SoundInfo *soundInfo, u32 counter, u32 frame, u32 samples, u32 deadline)
{
    u32 reverb = soundInfo->reverb;
    u32 n, chan, divFreq;

    if (reverb != 0)
    {
        u32 r5 = frame;
        u32 r7 = (counter == 2) ? A(soundInfo->pcmBuffer) : frame + samples;
        u32 r4 = samples, old;

        do
        {
            u32 sum = Ldsb(r5 + PCM_DMA_BUF_SIZE) + Ldsb(r5) + Ldsb(r7 + PCM_DMA_BUF_SIZE) + Ldsb(r7);
            u32 v = Asr(sum * reverb, 9);

            r7++;
            if (v & 0x80)
                v++;
            St8(r5 + PCM_DMA_BUF_SIZE, v);
            St8(r5, v);
            r5++;
            old = r4;
            r4--;
        } while (GtAfterSub(old, 1));
    }
    else
    {
        u32 r5 = frame;
        u32 r6 = frame + PCM_DMA_BUF_SIZE;
        u32 r1 = samples >> 4, old;

        if (samples & 4)
        {
            St32(r5, 0); r5 += 4;
            St32(r6, 0); r6 += 4;
        }
        if (samples & 8)
        {
            St32(r5, 0); r5 += 4;
            St32(r6, 0); r6 += 4;
            St32(r5, 0); r5 += 4;
            St32(r6, 0); r6 += 4;
        }
        do
        {
            St32(r5, 0); r5 += 4;
            St32(r6, 0); r6 += 4;
            St32(r5, 0); r5 += 4;
            St32(r6, 0); r6 += 4;
            St32(r5, 0); r5 += 4;
            St32(r6, 0); r6 += 4;
            St32(r5, 0); r5 += 4;
            St32(r6, 0); r6 += 4;
            old = r1;
            r1--;
        } while (GtAfterSub(old, 1));
    }

    divFreq = (u32)soundInfo->divFreq;
    n = soundInfo->maxChans;
    chan = A(soundInfo) + 0x50;
    for (;;)
    {
        u32 wav = Ld32Field(chan + CH_WAV);
        u32 st;

        if (deadline != 0 && CurrentLine() >= deadline)
            break;
        st = Ld8(chan + CH_STATUS);
        if (st & 0xC7)
            MixChannel(soundInfo, chan, wav, st, frame, samples, divFreq);
        if (!GtAfterSub(n, 1))
            break;
        n--;
        chan += 0x40;
    }
}

/* ------------------------------------------------------------------ */
/* SoundMain                                                           */

void SoundMain(void)
{
    struct SoundInfo *soundInfo = SOUND_INFO_PTR;
    u32 deadline, samples, frame, counter;

    if (soundInfo == NULL || soundInfo->ident != ID_NUMBER)
        return;
    soundInfo->ident = ID_NUMBER + 1;

    deadline = soundInfo->maxLines;
    if (deadline != 0)
        deadline += CurrentLine();

    if (soundInfo->func != 0)
        ((M4aPlayerFunc)(uintptr_t)soundInfo->func)((struct MusicPlayerInfo *)(uintptr_t)soundInfo->intp);
    soundInfo->CgbSound();

    samples = (u32)soundInfo->pcmSamplesPerVBlank;
    frame = A(soundInfo->pcmBuffer);
    counter = soundInfo->pcmDmaCounter;
    if (counter > 1)
        frame += (soundInfo->pcmDmaPeriod - (counter - 1)) * samples;

    SoundMainRAM_C(soundInfo, counter, frame, samples, deadline);

    soundInfo->ident = ID_NUMBER;
}
