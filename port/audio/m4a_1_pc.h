/*
 * FE8 PC port: the C entry points of port/audio/m4a_1_pc.c, the C version of
 * src/m4a_1.s. Signatures follow the asm's registers, not the prototypes in
 * include/gba/m4a_internal.h (port-plan/audio-design.md 2.2, rule 2).
 * Include after gba/m4a_internal.h, with that header's MPlayMain, ply_note
 * and SoundMainBTM prototypes renamed (see m4a_1_pc.c).
 */
#ifndef GUARD_PORT_AUDIO_M4A_1_PC_H
#define GUARD_PORT_AUDIO_M4A_1_PC_H

/* Called through SoundInfo.func / MusicPlayerInfo.func with r0 = player. */
void MPlayMain(struct MusicPlayerInfo *mplayInfo);
/* Called through SoundInfo.plynote: r0 = note-length index, r1 = player, r2 = track. */
void ply_note(u32 lenIdx, struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track);
/* Jump table entry 35 (Clear64byte): zeroes 64 bytes. */
void SoundMainBTM(void *addr);
/* asm: r4 = channel (SoundChannel or CgbChannel), r5 = track. */
void ChnVolSetAsm(void *chan, struct MusicPlayerTrack *track);
/* asm: r1 = track. */
void clear_modM(struct MusicPlayerTrack *track);
/* asm: r1 = track; returns r3 (the byte, 0 if chk_adr_r2 refuses it). */
u32 ld_r3_tp_adr_i(struct MusicPlayerTrack *track);
u32 ld_r3_tp_adr_i_unchecked(struct MusicPlayerTrack *track);
/* asm: r2 = address; nonzero if a value read there is kept. */
int chk_adr_r2(u32 addr);
/* asm: r2 = address; returns r3. */
u32 ldrb_r3_r2(u32 addr);

/* Extra window that chk_adr_r2 accepts below 0x02000000: [lo, hi), for a
 * host image loaded that low. Empty by default and not needed now: the
 * Linux and Windows builds both link the image at 0x10000000. */
extern u32 gPortM4aHostImageLo;
extern u32 gPortM4aHostImageHi;

#endif /* GUARD_PORT_AUDIO_M4A_1_PC_H */
