#include "global.h"

/*
 * Reforged Options, class skills: the Astra skill's battle animation (v1.12.0; the owner's request of
 * 2026-11-08: "slightly faded versions of the unit all doing the crit at different speeds but land
 * together as one hit ultimately"). Two same-line hooks of include/bmbattle.h, nothing on the GBA build:
 *
 *   RFO_ASTRA_HITS(hits)  src/banim-battleparse.c, ParseBattleHitToBanimCmd. The battle's hits still hold
 *     Astra's 5 strikes (gBattleHitArray: the results, the arena, EXP are untouched); the animation reads
 *     a copy in which each run of consecutive Astra strikes (BATTLE_HIT_ATTR_5) of the same side is one
 *     strike: one animation round (the crit animation, RFO_SKILL_ASTRA_ANIM), one HP drop of the strikes
 *     that landed added up. A Devil, HP-steal (Nosferatu, Runesword), Dual Guard (ATTR_17) or Petrify
 *     strike is never merged: such runs keep one round a strike, as before. The merged rounds are noted
 *     in gRfoAstraRoundPc (round -> strikes) and gRfoAstraSidePc (round -> attacking side), never in
 *     gAnimRoundData (EkrCheckAttackRound compares its raw values). Fewer rounds and HP entries than
 *     before, so gAnimRoundData's 9 rounds and gEfxHpLut's 20 entries stay in bounds.
 *
 *   RFO_ASTRA_COPIES()  src/banim-ekrbattle.c, MainUpdateEkrBattle, after BattleAIS_ExecCommands. During a
 *     merged round, 4 semi-transparent copies (OBJ mode 1; the battle's blend is EVA 10 / EVB 6 on BG2,
 *     BG3 and the backdrop) of the attacker's front anim replay its wind-up at 1.33x, 1.6x, 2x and 3x speed,
 *     each starting later, so that all of them reach the impact frame (the script's C08-C0C) on the tick
 *     the real one does, and vanish there. The timeline is the round's own script (pScrStart) read once,
 *     in script time: the real anim's place in it is found from its pScrCurrent and timer each tick, so
 *     the copies stop when the real anim waits on a blocking command. Pure drawing: the real anims' state
 *     is only read. A copy's frame is drawn only when its image sheet is the one the side has in VRAM
 *     (gpImgSheet: one 8 KB sheet a side), never an affine frame or one with OBJs already blended.
 */

#if PORT_PLAYED_BUILD

#include <string.h>

#include "hardware.h"
#include "variables.h"
#include "ctc.h"
#include "anime.h"
#include "bmbattle.h"
#include "ekrbattle.h"

#include "port_hw.h"
#include "port_reforged.h"
#include "reforged_skills.h"

/* port/render/obj_line.c: 1 = the per-line OBJ cycle budget (mGBA's); off while copies are drawn */
extern int gRenderObjCycleLimit;

u8 gRfoAstraRoundPc[BATTLE_HIT_MAX]; /* animation round -> Astra strikes merged into it (0: none) */
s8 gRfoAstraSidePc[BATTLE_HIT_MAX];  /* animation round -> the attacking side (EKR_POS_L/R) */
u8 gRfoAstraSrcPc[BATTLE_HIT_MAX];   /* animation round -> its first hit in gBattleHitArray (skill banners) */

static struct BattleHit sMergedHits[BATTLE_HIT_MAX];

#define ASTRA_NO_MERGE (BATTLE_HIT_ATTR_DEVIL | BATTLE_HIT_ATTR_HPSTEAL | BATTLE_HIT_ATTR_17 | BATTLE_HIT_ATTR_PETRIFY)

static int AstraMergeable(const struct BattleHit *h)
{
    return !(h->info & BATTLE_HIT_INFO_END) && (h->attributes & BATTLE_HIT_ATTR_5) && !(h->attributes & ASTRA_NO_MERGE);
}

static void AstraBattleReset(void);

struct BattleHit *Reforged_AstraMergeHits(struct BattleHit *hits)
{
    int i = 0, n = 0, merged = 0;

    AstraBattleReset(); /* a new battle (or the arena's next round): nothing carried over */
    memset(gRfoAstraRoundPc, 0, sizeof(gRfoAstraRoundPc));
    memset(gRfoAstraSidePc, -1, sizeof(gRfoAstraSidePc));
    for (i = 0; i < BATTLE_HIT_MAX; i++)
        gRfoAstraSrcPc[i] = (u8)i; /* no merge: round r is hit r */
    i = 0;
    Reforged_SkillBannerBattleStart();

    while (i < BATTLE_HIT_MAX - 1 && !(hits[i].info & BATTLE_HIT_INFO_END)) {
        struct BattleHit h = hits[i];
        int run = 1;

        if (AstraMergeable(&hits[i]))
            while (i + run < BATTLE_HIT_MAX - 1 && AstraMergeable(&hits[i + run])
                   && !((hits[i + run].info ^ hits[i].info) & BATTLE_HIT_INFO_RETALIATION))
                run++;

        if (run > 1) {
            u32 attrs = 0, info = 0;
            int k, landed = 0, damage = 0;

            for (k = 0; k < run; k++) {
                const struct BattleHit *s = &hits[i + k];

                info |= s->info;
                if (s->attributes & BATTLE_HIT_ATTR_MISS)
                    continue;
                landed++;
                attrs |= s->attributes;
                damage += s->hpChange;
            }

            if (landed) {
                attrs &= ~BATTLE_HIT_ATTR_MISS;
                /* Great Shield only when no strike hurt: its flag skips the HP bar (NewEfxHpBar), and the
                   HP table would then be one entry behind for the rest of the battle */
                if (damage > 0)
                    attrs &= ~BATTLE_HIT_ATTR_GREATSHLD;
                h.attributes = attrs | BATTLE_HIT_ATTR_5;
                h.hpChange = damage > 127 ? 127 : damage;
            } else {
                h.hpChange = 0;
            }
            h.info = info & ~BATTLE_HIT_INFO_END;

            gRfoAstraRoundPc[n] = (u8)run;
            gRfoAstraSidePc[n] = (gBanimPositionIsEnemy[EKR_POS_L] == ((h.info & BATTLE_HIT_INFO_RETALIATION) ? 1 : 0))
                                 ? EKR_POS_L : EKR_POS_R;
            merged = 1;
        }

        gRfoAstraSrcPc[n] = (u8)i;
        sMergedHits[n++] = h;
        i += run;
    }

    if (!merged)
        return hits;

    memset(&sMergedHits[n], 0, sizeof(sMergedHits[n]));
    if (hits[i].info & BATTLE_HIT_INFO_END)
        sMergedHits[n] = hits[i];
    sMergedHits[n].info |= BATTLE_HIT_INFO_END;

    {
        static int sLogged;
        if (sLogged < 20) {
            sLogged++;
            PortLog("[reforged] skills: Astra: %d hits shown as %d animation rounds\n", i, n);
        }
    }
    return sMergedHits;
}

/* ---- the faded copies ---- */

#define ASTRA_COPIES 4
#define ASTRA_STEPS_MAX 160
#define ASTRA_FRAMES_MAX 96
#define ASTRA_OAM_KEEP 24 /* OAM entries left for what comes after (PushSpriteLayerObjects(0xD)) */

struct AstraStep {
    const u32 *after; /* pScrCurrent once the instruction is read */
    s16 t;            /* its start, in script ticks */
    s16 d;            /* its length (the timer it sets) */
};

struct AstraFrame {
    s16 t;
    const void *sheet;
    u32 sprOff; /* sprite data offset into the anim's pSpriteDataPool */
    int objs;   /* its plain OBJs (AstraFrameObjs) */
};

#define ASTRA_OBJS_MAX 64
static int AstraFrameObjs(const struct AnimSpriteData *spr, struct AnimSpriteData *out);

static struct {
    const u32 *scr; /* the script read (pScrStart); NULL: none */
    int ok;         /* an impact command was found */
    int impact;     /* the impact's script tick */
    int converge;   /* T: where the copies meet the unit (AstraChooseConverge; 0: not chosen yet) */
    int nSteps, nFrames;
    struct AstraStep steps[ASTRA_STEPS_MAX];
    struct AstraFrame frames[ASTRA_FRAMES_MAX];
} sTl;

static int sCycleOffHold;   /* frames left with the OBJ cycle budget off */
static int sCycleSaved = 1; /* gRenderObjCycleLimit before */

static void AstraAddStep(const u32 *after, int t, int d)
{
    if (sTl.nSteps < ASTRA_STEPS_MAX) {
        sTl.steps[sTl.nSteps].after = after;
        sTl.steps[sTl.nSteps].t = (s16)t;
        sTl.steps[sTl.nSteps].d = (s16)d;
        sTl.nSteps++;
    }
}

/* Reads the round's script up to its first C08-C0C (the impact): the instructions as AnimInterpret
   (src/animedrv.c) runs them. A frame, wait or move lasts its delay; a command lasts one tick; a blocking
   command (01-05, 13, 18, 2D, 39, 52) none in script time (the anim waits there, the copies with it). */
static void AstraReadScript(const u32 *scr)
{
    const u32 *p = scr;
    int t = 0, n;

    memset(&sTl, 0, sizeof(sTl));
    sTl.scr = scr;

    for (n = 0; n < 512 && sTl.nSteps < ASTRA_STEPS_MAX - 2 && t < 2000; n++) {
        u32 ins = *p;
        int d;

        if (!ANINS_IS_NOT_FORCESPRITE(ins))
            return;
        if (ANINS_IS_PTRINS(ins)) {
            if (ANINS_PTRINS_GET_TYPE(ins) != 0)
                return;
            p++; /* a function call: no time */
            continue;
        }

        switch (ANINS_GET_TYPE(ins)) {
        case ANIM_INS_TYPE_FRAME:
            d = ANINS_FRAME_GET_DELAY(ins);
            if (sTl.nFrames < ASTRA_FRAMES_MAX) {
                sTl.frames[sTl.nFrames].t = (s16)t;
                sTl.frames[sTl.nFrames].sheet = (const void *)p[1];
                sTl.frames[sTl.nFrames].sprOff = p[2];
                sTl.nFrames++;
            }
            AstraAddStep(p + 3, t, d);
            t += d;
            p += 3;
            break;

        case ANIM_INS_TYPE_WAIT:
            d = ANINS_WAIT_GET_DELAY(ins);
            AstraAddStep(p + 1, t, d);
            t += d;
            p++;
            break;

        case ANIM_INS_TYPE_MOVE:
            d = ANINS_MOVE_GET_DELAY(ins);
            AstraAddStep(p + 1, t, d);
            t += d;
            p++;
            break;

        case ANIM_INS_TYPE_COMMAND:
            switch (ANINS_COMMAND_GET_ID(ins)) {
            case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C:
                AstraAddStep(p + 1, t, 1);
                sTl.impact = t;
                sTl.ok = sTl.nFrames > 0 && t > 0;
                return;

            case ANIM_CMD_WAIT_01: case ANIM_CMD_WAIT_02: case ANIM_CMD_WAIT_03: case ANIM_CMD_WAIT_04:
            case ANIM_CMD_WAIT_05: case ANIM_CMD_WAIT_13: case ANIM_CMD_WAIT_18: case ANIM_CMD_WAIT_2D:
            case ANIM_CMD_WAIT_39: case ANIM_CMD_WAIT_52:
                AstraAddStep(p, t, 1);     /* waiting on it */
                AstraAddStep(p + 1, t, 1); /* let go (the handler stepped past it) */
                p++;
                break;

            default:
                AstraAddStep(p + 1, t, 1);
                t += 1;
                p++;
                break;
            }
            break;

        default: /* stop, end, loop: no impact before them */
            return;
        }
    }
}

/* the real anim's script tick, or -1 when it is not on the read part of the script */
static int AstraScriptTick(const struct Anim *anim)
{
    int i;

    for (i = 0; i < sTl.nSteps; i++) {
        const struct AstraStep *s = &sTl.steps[i];

        if (s->after == anim->pScrCurrent) {
            int e = s->d - anim->timer;

            if (e < 0)
                e = 0;
            if (s->d > 0 && e > s->d - 1)
                e = s->d - 1;
            return s->t + e;
        }
    }
    return -1;
}

static const struct AstraFrame *AstraFrameAt(int u)
{
    const struct AstraFrame *f = NULL;
    int i;

    for (i = 0; i < sTl.nFrames && sTl.frames[i].t <= u; i++)
        f = &sTl.frames[i];
    return f;
}

/* speeds 4/3, 8/5, 2, 3 */
static const u8 sSpeedNum[ASTRA_COPIES] = { 4, 8, 2, 3 };
static const u8 sSpeedDen[ASTRA_COPIES] = { 3, 5, 1, 1 };

/* Where the copies meet the unit. Only one image sheet a side is in VRAM, so a copy shows only while
   its (earlier) frame is on the sheet of the real anim's frame. Most crits keep one sheet up to the
   blow: the copies meet at the impact itself. Some change sheets on the way (the Hero leaps out of
   the screen on a sheet of its own and falls back for the blow): meeting at the impact would cut every
   copy off at that change, so the meeting point is the frame start, up to the impact, that lets the
   copies show the most (OBJs x ticks; ties: the later one); they then join the unit there and the blow
   is one. */
static void AstraChooseConverge(const void *pool)
{
    int best = sTl.impact, bestScore = -1, k;
    struct AnimSpriteData tmp[ASTRA_OBJS_MAX + 1];

    for (k = 0; k < sTl.nFrames; k++)
        sTl.frames[k].objs = AstraFrameObjs((const struct AnimSpriteData *)((const u8 *)pool + sTl.frames[k].sprOff), tmp);

    for (k = sTl.nFrames; k >= 1; k--) {
        int C = k == sTl.nFrames ? sTl.impact : sTl.frames[k].t;
        int score = 0, tau, j;

        if (C <= 0 || C > sTl.impact)
            continue;
        for (tau = 0; tau < C; tau++) {
            const struct AstraFrame *real = AstraFrameAt(tau);

            for (j = 0; j < ASTRA_COPIES; j++) {
                int u = C - ((C - tau) * sSpeedNum[j]) / sSpeedDen[j];
                const struct AstraFrame *f;

                if (u < 0)
                    continue;
                f = AstraFrameAt(u);
                if (f != NULL && real != NULL && f->sheet == real->sheet)
                    score += f->objs;
            }
        }
        if (score > bestScore) {
            bestScore = score;
            best = C;
        }
    }
    sTl.converge = best;
    {
        static int sLogged;
        if (sLogged < 20) {
            sLogged++;
            PortLog("[reforged] skills: Astra copies: impact at tick %d, the copies meet the unit at tick %d\n",
                    sTl.impact, best);
        }
    }
}

/* Copies the frame's plain OBJs into out (ended by header 1, as AnimDisplay reads it) and returns how
   many: the affine header (its matrices) and every affine, blended or window OBJ (a thrown weapon that
   spins, a flash) are left out, so AnimDisplay appends no matrix and the copy's own mode 1 adds cleanly. */
static int AstraFrameObjs(const struct AnimSpriteData *spr, struct AnimSpriteData *out)
{
    int n = 0, guard = 0;

    if (spr == NULL)
        return 0;
    if ((spr->header & ~0xFFFF) == 0xFFFF0000)
        spr += spr->header & 0xFFFF;
    for (; spr->header != 1 && guard < 256; spr++, guard++) {
        if (spr->header & (0x0100 | OAM0_BLEND | OAM0_WINDOW))
            continue;
        if (n >= ASTRA_OBJS_MAX)
            break;
        out[n++] = *spr;
    }
    out[n].header = 1;
    return n;
}

static int AstraDrawSide(int pos)
{
    struct Anim *anim = gAnims[pos * 2];
    int r, tau, T, j, drawn = 0;

    if (anim == NULL || ANIM_IS_DISABLED(anim) || (anim->state & ANIM_BIT_HIDDEN))
        return 0;
    r = (int)(u16)anim->nextRoundId - 1;
    if (r < 0 || r >= BATTLE_HIT_MAX || gRfoAstraRoundPc[r] == 0 || gRfoAstraSidePc[r] != pos)
        return 0;
    if (anim->currentRoundType != GetBattleAnimRoundType(r * 2 + pos))
        return 0;

    if (sTl.scr != anim->pScrStart)
        AstraReadScript(anim->pScrStart);
    if (!sTl.ok)
        return 0;

    if (sTl.converge == 0)
        AstraChooseConverge(anim->pSpriteDataPool);
    T = sTl.converge;
    tau = AstraScriptTick(anim);
    if (tau < 0 || tau >= T)
        return 0; /* not started, or the impact: the copies have landed */

    for (j = 0; j < ASTRA_COPIES; j++) {
        const struct AstraFrame *f;
        struct AnimSpriteData spr[ASTRA_OBJS_MAX + 1];
        struct Anim copy;
        int u, objs, used, dx, mag;

        u = T - ((T - tau) * sSpeedNum[j]) / sSpeedDen[j];
        if (u < 0)
            continue; /* this copy has not set off yet */
        f = AstraFrameAt(u);
        if (f == NULL || f->sheet != gpImgSheet[pos])
            continue; /* its frame's tiles are not in VRAM */
        objs = AstraFrameObjs((const struct AnimSpriteData *)((const u8 *)anim->pSpriteDataPool + f->sprOff), spr);
        if (objs <= 0)
            continue;
        used = (int)(gOamHiPutIt - (u32 *)gOam) / 2;
        if (used + objs > 128 - ASTRA_OAM_KEEP)
            break;

        /* a small spread, one copy behind and one ahead in turn, closing to 0 at the impact */
        mag = ((T - tau) * (j + 2)) / 6;
        if (mag > 32)
            mag = 32;
        dx = (j & 1) ? mag : -mag;
        if (pos == EKR_POS_R)
            dx = -dx;

        copy.pSpriteData = spr;
        copy.xPosition = anim->xPosition + dx;
        copy.yPosition = anim->yPosition;
        copy.oamBase = (anim->oamBase & ~(OAM0_WINDOW | OAM0_BLEND)) | OAM0_BLEND;
        copy.oam2Base = anim->oam2Base;
        AnimDisplay(&copy);
        drawn++;
    }
    return drawn;
}

/* The fade. By the wind-up the battle's blend is no effect, EVA 16 / EVB 0, the backdrop alone as the
   second target (BLDCNT 0x203F): a mode 1 OBJ would show at full strength. While copies are drawn and
   no blend effect runs (so only mode 1 OBJs blend at all), the second target gets BG2, BG3 (the battle
   background) and the backdrop, at EVA 10 / EVB 6 (what ekrBattleIntro sets); on the first tick without
   copies the old values come back, unless something else has changed them in between. */
#define ASTRA_EVA 10
#define ASTRA_EVB 6

static int sBlendSet;
static u16 sBlendSavedCnt, sBlendOurCnt;
static u8 sBlendSavedA, sBlendSavedB;

static void AstraBlend(int drawn)
{
    u16 *cnt = (u16 *)&gLCDControlBuffer.bldcnt;

    if (drawn && !sBlendSet) {
        if (gLCDControlBuffer.bldcnt.effect != BLEND_EFFECT_NONE)
            return;
        sBlendSavedCnt = *cnt;
        sBlendSavedA = gLCDControlBuffer.blendCoeffA;
        sBlendSavedB = gLCDControlBuffer.blendCoeffB;
        gLCDControlBuffer.bldcnt.target2_bg2_on = 1;
        gLCDControlBuffer.bldcnt.target2_bg3_on = 1;
        gLCDControlBuffer.bldcnt.target2_bd_on = 1;
        gLCDControlBuffer.blendCoeffA = ASTRA_EVA;
        gLCDControlBuffer.blendCoeffB = ASTRA_EVB;
        sBlendOurCnt = *cnt;
        sBlendSet = 1;
    } else if (!drawn && sBlendSet) {
        sBlendSet = 0;
        if (*cnt == sBlendOurCnt && gLCDControlBuffer.blendCoeffA == ASTRA_EVA && gLCDControlBuffer.blendCoeffB == ASTRA_EVB) {
            *cnt = sBlendSavedCnt;
            gLCDControlBuffer.blendCoeffA = sBlendSavedA;
            gLCDControlBuffer.blendCoeffB = sBlendSavedB;
        }
    }
}

/* At each parse: the script timeline is cached by its buffer's address, which every battle reuses,
   so it is read again; and a battle left while copies showed (its loop stopped) gives the OBJ cycle
   budget back here (the battle's own blend set-up runs again at its start). */
static void AstraBattleReset(void)
{
    memset(&sTl, 0, sizeof(sTl));
    if (sCycleOffHold > 0)
    {
        sCycleOffHold = 0;
        gRenderObjCycleLimit = sCycleSaved;
    }
    sBlendSet = 0;
}

void Reforged_AstraCopiesDraw(void)
{
    int drawn = 0, r;

    for (r = 0; r < BATTLE_HIT_MAX; r++)
        if (gRfoAstraRoundPc[r]) {
            drawn = AstraDrawSide(EKR_POS_L) + AstraDrawSide(EKR_POS_R);
            break;
        }

    AstraBlend(drawn);

    /* the copies overlap the unit on every line: without this mGBA's per-line OBJ budget drops parts */
    if (drawn) {
        if (sCycleOffHold == 0) {
            sCycleSaved = gRenderObjCycleLimit;
            gRenderObjCycleLimit = 0;
        }
        sCycleOffHold = 4;
    } else if (sCycleOffHold > 0 && --sCycleOffHold == 0) {
        gRenderObjCycleLimit = sCycleSaved;
    }
}

#endif /* PORT_PLAYED_BUILD */
