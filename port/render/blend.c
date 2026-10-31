#include "obj_line.h"

/*
 * Software renderer, layer search and blend unit
 * (port-plan/render-design-obj.md sections 6, 7 and 8).
 *
 * Two arithmetic modes (design 7.6):
 *   RENDER_BLEND_HW    GBATEK's 5-bit formulas, shifts round down.
 *   RENDER_BLEND_MGBA8 desktop mGBA 0.10.3: both inputs expanded with the D3
 *                      rule, blended on 8-bit channels packed 0x00BBGGRR the
 *                      way mGBA does it (software-private.h _brighten,
 *                      _darken; mColorMix5Bit), and the top 5 bits kept.
 *                      Note mGBA's packed darken rounds the green and blue
 *                      channels up and the red channel down; that is copied.
 */

int gRenderBlendMode = RENDER_BLEND_HW;
int gRenderSemiIgnoresWinFx = RENDER_SEMI_OBJ_IGNORES_WIN_FX;

/* EVA, EVB and EVY: 5-bit fields, 17-31 act as 16 (GBATEK). */
int RenderClampEv(int v)
{
    v &= 0x1F;
    return v > 16 ? 16 : v;
}

uint16_t RenderBlendAlpha(uint16_t a, uint16_t b, int eva, int evb)
{
    unsigned out = 0;
    int s;

    eva = RenderClampEv(eva);
    evb = RenderClampEv(evb);
    for (s = 0; s <= 10; s += 5)
    {
        unsigned v = (((a >> s) & 31) * (unsigned)eva + ((b >> s) & 31) * (unsigned)evb) >> 4;

        out |= (v > 31 ? 31 : v) << s;
    }
    return (uint16_t)out;
}

uint16_t RenderBrighten(uint16_t c, int evy)
{
    unsigned out = 0;
    int s;

    evy = RenderClampEv(evy);
    for (s = 0; s <= 10; s += 5)
    {
        unsigned v = (c >> s) & 31;

        out |= (v + (((31 - v) * (unsigned)evy) >> 4)) << s;
    }
    return (uint16_t)out;
}

uint16_t RenderDarken(uint16_t c, int evy)
{
    unsigned out = 0;
    int s;

    evy = RenderClampEv(evy);
    for (s = 0; s <= 10; s += 5)
    {
        unsigned v = (c >> s) & 31;

        out |= (v - ((v * (unsigned)evy) >> 4)) << s;
    }
    return (uint16_t)out;
}

/* BGR555 to mGBA's 32-bit colour, 0x00BBGGRR, using the D3 expansion. */
static uint32_t Mgba8Pack(uint16_t c)
{
    return (uint32_t)RenderExpand5(c & 31) | ((uint32_t)RenderExpand5((c >> 5) & 31) << 8)
        | ((uint32_t)RenderExpand5((c >> 10) & 31) << 16);
}

/* Top 5 bits of each 8-bit channel: how a screenshot is reduced to 15 bits. */
static uint16_t Mgba8Unpack(uint32_t c)
{
    return (uint16_t)(((c & 0xFF) >> 3) | (((c >> 8) & 0xFF) >> 3) << 5 | (((c >> 16) & 0xFF) >> 3) << 10);
}

uint16_t RenderBlendAlphaMgba8(uint16_t a, uint16_t b, int eva, int evb)
{
    uint32_t ca = Mgba8Pack(a);
    uint32_t cb = Mgba8Pack(b);
    uint32_t out = 0;
    int s;

    eva = RenderClampEv(eva);
    evb = RenderClampEv(evb);
    /* mColorMix5Bit: per channel (x * eva + y * evb) / 16, clamped to 255. */
    for (s = 0; s <= 16; s += 8)
    {
        uint32_t v = (((ca >> s) & 0xFF) * (uint32_t)eva + ((cb >> s) & 0xFF) * (uint32_t)evb) / 16;

        out |= (v > 0xFF ? 0xFF : v) << s;
    }
    return Mgba8Unpack(out);
}

uint16_t RenderBrightenMgba8(uint16_t c, int evy)
{
    uint32_t color = Mgba8Pack(c);
    uint32_t y = (uint32_t)RenderClampEv(evy);
    uint32_t out = 0;
    uint32_t a;

    a = color & 0xFF;
    out |= (a + ((0xFF - a) * y) / 16) & 0xFF;
    a = color & 0xFF00;
    out |= (a + ((0xFF00 - a) * y) / 16) & 0xFF00;
    a = color & 0xFF0000;
    out |= (a + ((0xFF0000 - a) * y) / 16) & 0xFF0000;
    return Mgba8Unpack(out);
}

uint16_t RenderDarkenMgba8(uint16_t c, int evy)
{
    uint32_t color = Mgba8Pack(c);
    uint32_t y = (uint32_t)RenderClampEv(evy);
    uint32_t out = 0;
    uint32_t a;

    a = color & 0xFF;
    out |= (a - (a * y) / 16) & 0xFF;
    a = color & 0xFF00;
    out |= (a - (a * y) / 16) & 0xFF00;
    a = color & 0xFF0000;
    out |= (a - (a * y) / 16) & 0xFF0000;
    return Mgba8Unpack(out);
}

static uint16_t FxAlpha(uint16_t a, uint16_t b, int eva, int evb)
{
    if (gRenderBlendMode == RENDER_BLEND_MGBA8)
        return RenderBlendAlphaMgba8(a, b, eva, evb);
    return RenderBlendAlpha(a, b, eva, evb);
}

static uint16_t FxBrighten(uint16_t c, int evy)
{
    if (gRenderBlendMode == RENDER_BLEND_MGBA8)
        return RenderBrightenMgba8(c, evy);
    return RenderBrighten(c, evy);
}

static uint16_t FxDarken(uint16_t c, int evy)
{
    if (gRenderBlendMode == RENDER_BLEND_MGBA8)
        return RenderDarkenMgba8(c, evy);
    return RenderDarken(c, evy);
}

/*
 * Compose one line (design 6): forced blank, then per pixel the window mask,
 * the top two layers, and the blend effect. bg may be NULL (no BGs), obj may
 * be NULL (no OBJs), mask may be NULL (everything enabled).
 */
void RenderComposeLine(int line, const struct RenderFxRegs *fx, const struct RenderComposeBg *bg,
    uint16_t backdrop, const struct RenderObjLine *obj, const uint8_t *mask, uint16_t *dst)
{
    int order[4]; /* drawn BGs, front to back: priority, then BG number */
    int nOrder = 0;
    int effect = (fx->bldcnt >> 6) & 3;
    unsigned target1 = fx->bldcnt & 0x3F;
    unsigned target2 = (fx->bldcnt >> 8) & 0x3F;
    int eva = RenderClampEv(fx->bldalpha);
    int evb = RenderClampEv(fx->bldalpha >> 8);
    int evy = RenderClampEv(fx->bldy);
    int p, n, x;

    (void)line;
    backdrop &= 0x7FFF;

    if (fx->dispcnt & 0x80)
    {
        for (x = 0; x < RENDER_WIDTH; x++)
            dst[x] = RENDER_WHITE;
        return;
    }

    if (bg != NULL)
    {
        for (p = 0; p < 4; p++)
        {
            for (n = 0; n < 4; n++)
            {
                if (bg->on[n] && bg->color[n] != NULL && (bg->prio[n] & 3) == p)
                    order[nOrder++] = n;
            }
        }
    }

    for (x = 0; x < RENDER_WIDTH; x++)
    {
        unsigned w = mask != NULL ? mask[x] : RENDER_WIN_ALL;
        int objOk = obj != NULL && obj->has[x] && (w & RENDER_WIN_OBJ);
        int layer[2] = { RENDER_LAYER_NONE, RENDER_LAYER_NONE };
        uint16_t color[2] = { 0, 0 };
        int found = 0;
        int i;
        uint16_t out;

        /* Layer search: per priority the OBJ first, then BG0-3 (design 6 step 3). */
        for (i = 0; i <= nOrder && found < 2; i++)
        {
            if (objOk && (i == nOrder || obj->prio[x] <= (bg->prio[order[i]] & 3)))
            {
                layer[found] = RENDER_LAYER_OBJ;
                color[found] = obj->color[x];
                found++;
                objOk = 0;
                if (found == 2)
                    break;
            }
            if (i < nOrder)
            {
                uint16_t c;

                n = order[i];
                if (!(w & (1u << n)))
                    continue;
                c = bg->color[n][x];
                if (c & RENDER_TRANSPARENT)
                    continue;
                layer[found] = n;
                color[found] = c & 0x7FFF;
                found++;
            }
        }
        /* The backdrop is always present behind everything. */
        if (found < 2)
        {
            layer[found] = RENDER_LAYER_BD;
            color[found] = backdrop;
        }

        out = color[0];
        if (layer[0] == RENDER_LAYER_OBJ && obj->semi[x] && layer[1] != RENDER_LAYER_NONE
            && (target2 & (1u << layer[1])) && ((w & RENDER_WIN_FX) || gRenderSemiIgnoresWinFx))
        {
            /* Semi-transparent OBJ over a target-2 layer: alpha, whatever BLDCNT
               bit 4 and the effect field say, and no brightness (design 7.3). */
            out = FxAlpha(color[0], color[1], eva, evb);
        }
        else if ((w & RENDER_WIN_FX) && (target1 & (1u << layer[0])))
        {
            switch (effect)
            {
            case 1:
                if (layer[1] != RENDER_LAYER_NONE && (target2 & (1u << layer[1])))
                    out = FxAlpha(color[0], color[1], eva, evb);
                break;
            case 2:
                out = FxBrighten(color[0], evy);
                break;
            case 3:
                out = FxDarken(color[0], evy);
                break;
            }
        }
        dst[x] = out & 0x7FFF;
    }
}
